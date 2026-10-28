/**
 * @file	fac_bl_proto.c
 * @brief	CDC protocol version 1 (bootloader.md, section 7).
 *
 * Frame: SOF 0xA5, cmd, seq, len (u16 LE), payload, CRC-32 LE of cmd..payload.
 * One request at a time; every command can be repeated safely.
 */
#include "Bootloader/fac_bl_proto.h"
#include "Bootloader/fac_bl.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"
#include "Bootloader/fac_bl_crc.h"
#include "Bootloader/fac_bl_flash.h"
#include "Bootloader/fac_bl_port.h"
#include <stddef.h>
#include <string.h>

#define HEADER_SIZE		4U		// cmd, seq, len (2) stored at the start of the frame buffer
#define CRC_SIZE		4U
#define REPLY_MAX		(1U + FAC_BL_INFO_SIZE)	// status + largest reply payload

enum { RX_IDLE, RX_HEADER, RX_BODY, RX_DISCARD };

/* Received frame from cmd on: word aligned, so the payload and the WRITE data are aligned too */
static union {
	uint32_t align;
	uint8_t bytes[HEADER_SIZE + FAC_BL_MAX_PAYLOAD + CRC_SIZE];
} frame;

static struct {
	uint8_t state;
	uint16_t count;			// bytes stored in frame
	uint16_t length;		// payload length
	uint32_t last_byte_ms;
	bool junk;				// stray bytes seen while idle
} rx;

static struct {
	bool idle_return;		// 60 s without packets go back to the application
	bool erased;			// an ERASE ran in this session
	uint32_t erased_bytes;	// writable bytes from APP_BASE
	uint32_t last_frame_ms;
	bool reset_pending;
	uint32_t reset_at_ms;
} session;

static uint8_t reply[1U + HEADER_SIZE + REPLY_MAX + CRC_SIZE];	// SOF, header, payload, CRC

static void put16(uint8_t *p, uint16_t v) {
	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
	put16(p, (uint16_t) v);
	put16(p + 2, (uint16_t) (v >> 16));
}

static uint32_t get32(const uint8_t *p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t) p[3] << 24);
}

/* Sends a frame, waiting a little if the previous one is still leaving */
static void send(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t length) {
	reply[0] = FAC_BL_SOF;
	reply[1] = cmd;
	reply[2] = seq;
	put16(&reply[3], length);
	memcpy(&reply[5], payload, length);
	put32(&reply[5 + length], FAC_BL_crc32(0, &reply[1], HEADER_SIZE + length));

	uint32_t start = FAC_BL_port_millis();
	while (!FAC_BL_port_usb_send(reply, (uint16_t) (FAC_BL_FRAME_OVERHEAD + length))) {
		FAC_BL_port_watchdog_kick();
		if (FAC_BL_port_millis() - start >= FAC_BL_TX_TIMEOUT_MS)
			return;	// host not reading: the tool will repeat the request
	}
}

static void send_nak(uint8_t status, uint8_t seq) {
	send(FAC_BL_CMD_NAK, seq, &status, 1);
}

static uint8_t stay_to_status(uint8_t reason) {
	if (reason == FAC_BOOT_STAY_BAD_HEADER)
		return FAC_BL_BAD_HEADER;
	if (reason == FAC_BOOT_STAY_BAD_VECTORS)
		return FAC_BL_BAD_VECTORS;
	return FAC_BL_OK;
}

/* INFO: 44 bytes (bootloader.md, section 7.4) */
static uint16_t cmd_info(uint8_t *out) {
	FAC_BL_app_status_t app;
	FAC_BL_app_check(&app);
	volatile FAC_boot_shared_t *shared = FAC_BL_port_shared();

	memset(out, 0, 1U + FAC_BL_INFO_SIZE);
	uint8_t *info = out + 1;	// out[0] = FAC_BL_OK
	info[0] = FAC_BL_PROTOCOL_VERSION;
	info[1] = FAC_BOOT_HW_ID;
	put16(&info[2], FAC_BL_VERSION);
	put32(&info[4], FAC_BOOT_APP_BASE);
	put32(&info[8], FAC_BOOT_APP_MAX_SIZE);
	put16(&info[12], FAC_BOOT_PAGE_SIZE);
	put16(&info[14], FAC_BL_MAX_WRITE);
	info[16] = shared->stay_reason;
	info[17] = shared->boot_attempts;
	info[18] = app.state;
	put32(&info[20], app.record.app_size);
	put32(&info[24], app.record.app_crc);
	put16(&info[28], app.fw_version);
	memcpy(&info[32], FAC_BL_port_uid(), 12);
	return 1U + FAC_BL_INFO_SIZE;
}

/* ERASE: record page first, so an interrupted update never leaves a valid record */
static uint16_t cmd_erase(const uint8_t *payload, uint16_t length, uint8_t *out) {
	if (length != 4) {
		out[0] = FAC_BL_BAD_LENGTH;
		return 1;
	}
	uint32_t size = get32(payload);
	if (size == 0 || size > FAC_BOOT_APP_MAX_SIZE) {
		out[0] = FAC_BL_OUT_OF_RANGE;
		return 1;
	}
	uint32_t pages = (size + FAC_BOOT_PAGE_SIZE - 1U) / FAC_BOOT_PAGE_SIZE;
	session.erased = true;	// from now on the application may be gone
	session.erased_bytes = 0;
	out[0] = FAC_BL_flash_erase(FAC_BOOT_RECORD_ADDR, 1);
	if (out[0] == FAC_BL_OK)
		out[0] = FAC_BL_flash_erase(FAC_BOOT_APP_BASE, pages);
	if (out[0] != FAC_BL_OK)
		return 1;
	session.erased_bytes = pages * FAC_BOOT_PAGE_SIZE;
	put16(&out[1], (uint16_t) pages);
	return 3;
}

/* WRITE: offset from APP_BASE, inside the area erased in this session */
static uint16_t cmd_write(const uint8_t *payload, uint16_t length, uint8_t *out) {
	if (length < 8 || length - 4U > FAC_BL_MAX_WRITE) {
		out[0] = FAC_BL_BAD_LENGTH;
		return 1;
	}
	uint32_t offset = get32(payload);
	uint32_t count = length - 4U;
	if ((offset & 3U) != 0 || (count & 3U) != 0)
		out[0] = FAC_BL_MISALIGNED;
	else if (!session.erased)
		out[0] = FAC_BL_BAD_STATE;
	else if (offset > session.erased_bytes || count > session.erased_bytes - offset)
		out[0] = FAC_BL_OUT_OF_RANGE;
	else
		out[0] = FAC_BL_flash_write(FAC_BOOT_APP_BASE + offset, payload + 4, count);
	return 1;
}

/* VERIFY: CRC, header and vectors, then the record (does not need an ERASE) */
static uint16_t cmd_verify(const uint8_t *payload, uint16_t length, uint8_t *out) {
	if (length != 8) {
		out[0] = FAC_BL_BAD_LENGTH;
		return 1;
	}
	uint32_t size = get32(payload);
	uint32_t expected = get32(payload + 4);
	if ((size & 3U) != 0) {
		out[0] = FAC_BL_MISALIGNED;
		return 1;
	}
	if (size == 0 || size > FAC_BOOT_APP_MAX_SIZE) {
		out[0] = FAC_BL_OUT_OF_RANGE;
		return 1;
	}

	uint32_t crc = FAC_BL_crc32(0, FAC_BL_port_flash(FAC_BOOT_APP_BASE), size);
	put32(&out[1], crc);
	if (crc != expected) {
		out[0] = FAC_BL_CRC_MISMATCH;
		return 5;
	}
	out[0] = stay_to_status(FAC_BL_app_check_image(size));
	if (out[0] != FAC_BL_OK)
		return 5;

	FAC_boot_app_header_t header;
	FAC_BL_app_read_header(&header);
	FAC_boot_record_t record = {
		.magic = FAC_BOOT_MAGIC_RECORD,
		.app_size = size,
		.app_crc = crc,
		.fw_version = header.fw_version,
		.flags = 0,
	};
	record.record_crc = FAC_BL_crc32(0, &record, offsetof(FAC_boot_record_t, record_crc));
	out[0] = FAC_BL_flash_write_record(&record);
	return 5;
}

/* BOOT: reset into the application once the reply has left */
static uint16_t cmd_boot(uint16_t length, uint8_t *out, uint32_t now) {
	if (length != 0) {
		out[0] = FAC_BL_BAD_LENGTH;
		return 1;
	}
	FAC_BL_app_status_t app;
	FAC_BL_app_check(&app);
	if (app.reason != FAC_BOOT_STAY_NONE) {
		out[0] = FAC_BL_NO_VALID_APP;
		return 1;
	}
	volatile FAC_boot_shared_t *shared = FAC_BL_port_shared();
	shared->boot_attempts = 0;
	shared->request = 0;
	FAC_BL_shared_seal();
	session.reset_pending = true;
	session.reset_at_ms = now + FAC_BL_BOOT_RESET_DELAY_MS;
	out[0] = FAC_BL_OK;
	return 1;
}

/* A frame with a good CRC */
static void dispatch(uint32_t now) {
	uint8_t cmd = frame.bytes[0];
	uint8_t seq = frame.bytes[1];
	const uint8_t *payload = &frame.bytes[HEADER_SIZE];
	uint16_t length = rx.length;
	uint8_t out[REPLY_MAX];
	uint16_t out_length;

	session.last_frame_ms = now;

	switch (cmd) {
	case FAC_BL_CMD_INFO:
		if (length != 0) {
			out[0] = FAC_BL_BAD_LENGTH;
			out_length = 1;
		} else
			out_length = cmd_info(out);
		break;
	case FAC_BL_CMD_ERASE:
		out_length = cmd_erase(payload, length, out);
		break;
	case FAC_BL_CMD_WRITE:
		out_length = cmd_write(payload, length, out);
		break;
	case FAC_BL_CMD_VERIFY:
		out_length = cmd_verify(payload, length, out);
		break;
	case FAC_BL_CMD_BOOT:
		out_length = cmd_boot(length, out, now);
		break;
	default:
		out[0] = FAC_BL_UNKNOWN_COMMAND;
		out_length = 1;
		break;
	}
	send((uint8_t) (cmd | FAC_BL_REPLY_FLAG), seq, out, out_length);

	if (session.erased)
		FAC_BL_led_activity(now);	// LED follows the packets while writing (ERASE included)
}

void FAC_BL_proto_init(bool idle_return, uint32_t now) {
	memset(&rx, 0, sizeof rx);
	memset(&session, 0, sizeof session);
	session.idle_return = idle_return;
	session.last_frame_ms = now;
}

void FAC_BL_proto_byte(uint8_t byte, uint32_t now) {
	rx.last_byte_ms = now;
	switch (rx.state) {
	case RX_IDLE:
		if (byte == FAC_BL_SOF) {
			rx.state = RX_HEADER;
			rx.count = 0;
			rx.junk = false;	// the frame gets its own reply
		} else
			rx.junk = true;
		break;

	case RX_HEADER:
		frame.bytes[rx.count++] = byte;
		if (rx.count == HEADER_SIZE) {
			rx.length = (uint16_t) (frame.bytes[2] | (frame.bytes[3] << 8));
			rx.state = rx.length > FAC_BL_MAX_PAYLOAD ? RX_DISCARD : RX_BODY;
		}
		break;

	case RX_BODY:
		frame.bytes[rx.count++] = byte;
		if (rx.count == HEADER_SIZE + rx.length + CRC_SIZE) {
			rx.state = RX_IDLE;
			uint32_t received = get32(&frame.bytes[HEADER_SIZE + rx.length]);
			if (received != FAC_BL_crc32(0, frame.bytes, HEADER_SIZE + rx.length))
				send_nak(FAC_BL_BAD_CRC, frame.bytes[1]);
			else
				dispatch(now);
		}
		break;

	default:	// RX_DISCARD: drop everything until the line is silent
		break;
	}
}

void FAC_BL_proto_poll(uint32_t now) {
	uint32_t silence = now - rx.last_byte_ms;

	if (rx.state == RX_IDLE && rx.junk && silence >= FAC_BL_JUNK_SILENCE_MS) {
		rx.junk = false;
		send_nak(FAC_BL_NOT_A_FRAME, 0);	// lets the tool tell the bootloader from the application
	} else if (rx.state == RX_DISCARD && silence >= FAC_BL_JUNK_SILENCE_MS) {
		rx.state = RX_IDLE;
		send_nak(FAC_BL_BAD_LENGTH, frame.bytes[1]);
	} else if ((rx.state == RX_HEADER || rx.state == RX_BODY) && silence > FAC_BL_FRAME_GAP_MS) {
		rx.state = RX_IDLE;
		send_nak(FAC_BL_NOT_A_FRAME, 0);	// partial frame dropped
	}

	if (session.reset_pending && (int32_t) (now - session.reset_at_ms) >= 0) {
		session.reset_pending = false;
		FAC_BL_port_reset();
	}

	// entered on request and nothing done: back to the application after a minute
	if (session.idle_return && !session.erased && now - session.last_frame_ms >= FAC_BL_IDLE_RETURN_MS) {
		FAC_BL_app_status_t app;
		FAC_BL_app_check(&app);
		session.idle_return = false;
		if (app.reason == FAC_BOOT_STAY_NONE)
			FAC_BL_port_reset();
	}
}
