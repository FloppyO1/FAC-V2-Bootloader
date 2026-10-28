#include "helpers.h"
#include "sim_port.h"
#include "Bootloader/fac_bl.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_proto.h"
#include <stddef.h>
#include <string.h>

static uint8_t image[FAC_BOOT_APP_MAX_SIZE];

uint32_t ref_crc32(uint32_t crc, const void *data, uint32_t length) {
	const uint8_t *p = data;
	crc = ~crc;
	while (length--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++)
			crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
	}
	return ~crc;
}

img_opts_t img_defaults(void) {
	img_opts_t o = {
		.sp = 0x20004000, .entry = 0x08004101, .magic = FAC_BOOT_MAGIC_APP,
		.header_version = 1, .hw_id = 2, .fw_version = 20100,
		.link_address = FAC_BOOT_APP_BASE, .flags = 0,
	};
	return o;
}

void img_build(uint8_t *img, uint32_t size, const img_opts_t *o) {
	for (uint32_t i = 0; i < size; i++)	// payload pattern
		img[i] = (uint8_t) (i * 7U + (i >> 8));
	memcpy(img, &o->sp, 4);
	memcpy(img + 4, &o->entry, 4);
	FAC_boot_app_header_t h = {
		.magic = o->magic, .header_version = o->header_version, .hw_id = o->hw_id,
		.fw_version = o->fw_version, .link_address = o->link_address, .flags = o->flags,
	};
	memcpy(img + FAC_BOOT_VECTORS_SIZE, &h, sizeof h);
}

void rec_seal(FAC_boot_record_t *rec) {
	rec->record_crc = ref_crc32(0, rec, offsetof(FAC_boot_record_t, record_crc));
}

FAC_boot_record_t rec_make(const uint8_t *img, uint32_t size, uint16_t fw_version) {
	FAC_boot_record_t r = {
		.magic = FAC_BOOT_MAGIC_RECORD, .app_size = size,
		.app_crc = ref_crc32(0, img, size), .fw_version = fw_version, .flags = 0,
	};
	rec_seal(&r);
	return r;
}

void rec_write(const FAC_boot_record_t *rec) {
	sim_flash_load(FAC_BOOT_RECORD_ADDR, rec, sizeof *rec);
}

uint8_t *app_install(const img_opts_t *opts, uint32_t size, bool with_record, FAC_boot_record_t *rec) {
	img_build(image, size, opts);
	sim_flash_load(FAC_BOOT_APP_BASE, image, size);
	FAC_boot_record_t r = rec_make(image, size, opts->fw_version);
	if (with_record)
		rec_write(&r);
	if (rec)
		*rec = r;
	return image;
}

void app_poke32(uint32_t offset, uint32_t value) {
	sim_flash_load(FAC_BOOT_APP_BASE + offset, &value, 4);
}

uint32_t frame_build(uint8_t *out, uint8_t cmd, uint8_t seq, const void *payload, uint16_t length) {
	out[0] = FAC_BL_SOF;
	out[1] = cmd;
	out[2] = seq;
	out[3] = (uint8_t) length;
	out[4] = (uint8_t) (length >> 8);
	if (length)
		memcpy(out + 5, payload, length);
	uint32_t crc = ref_crc32(0, out + 1, 4U + length);
	memcpy(out + 5 + length, &crc, 4);	// little endian host
	return 9U + length;
}

uint32_t rd32(const uint8_t *p) {
	return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
}

uint16_t rd16(const uint8_t *p) {
	return (uint16_t) (p[0] | (p[1] << 8));
}

int replies_parse(reply_t *out, int max) {
	uint32_t pos = 0;
	int n = 0;
	while (pos < sim.tx_len) {
		if (n >= max || sim.tx_len - pos < 9 || sim.tx[pos] != FAC_BL_SOF)
			return -1;
		uint16_t len = rd16(&sim.tx[pos + 3]);
		if (len > sizeof out[0].payload || sim.tx_len - pos < 9U + len)
			return -1;
		if (rd32(&sim.tx[pos + 5 + len]) != ref_crc32(0, &sim.tx[pos + 1], 4U + len))
			return -1;
		out[n].cmd = sim.tx[pos + 1];
		out[n].seq = sim.tx[pos + 2];
		out[n].len = len;
		memcpy(out[n].payload, &sim.tx[pos + 5], len);
		n++;
		pos += 9U + len;
	}
	return n;
}

/* FAC_BL_init does not clear the ring and LED state (zeroed .bss on the board): bring them back
 * to "empty, LED off" through the public API, then forget what that did to the counters */
static void bl_quiesce(void) {
	sim.now = 100500;	// 500 ms into a second: LED off
	for (int i = 0; i < 4; i++)
		FAC_BL_loop();
	sim.now = 1000;
	sim.tx_len = sim.tx_calls = 0;
	sim.rearm_calls = sim.led_toggles = sim.wd_kicks = sim.reset_calls = 0;
	sim.busy_calls = 0;
	sim.busy_forever = false;
}

void bl_enter(uint8_t stay_reason) {
	bool busy = sim.busy_forever;
	uint32_t busy_calls = sim.busy_calls;
	bl_quiesce();
	sim.busy_forever = busy;
	sim.busy_calls = busy_calls;
	FAC_BL_shared_reset();
	FAC_BL_port_shared()->stay_reason = stay_reason;
	FAC_BL_shared_seal();
	FAC_BL_init();
}

void bl_feed(const void *data, uint32_t length) {
	const uint8_t *p = data;
	while (length) {
		uint32_t n = length > 64 ? 64 : length;
		FAC_BL_usb_received(p, n);
		p += n;
		length -= n;
	}
}

void bl_run(void) {
	FAC_BL_loop();
}

void bl_advance(uint32_t ms) {
	sim.now += ms;
	FAC_BL_loop();
}

int rq(uint8_t cmd, uint8_t seq, const void *payload, uint16_t length, reply_t *r) {
	uint8_t buf[FAC_BL_MAX_PAYLOAD + 16];
	uint32_t n = frame_build(buf, cmd, seq, payload, length);
	sim_tx_clear();
	bl_feed(buf, n);
	bl_run();
	reply_t all[8];
	int count = replies_parse(all, 8);
	if (count > 0 && r)
		*r = all[0];
	return count;
}
