/**
 * @file	fac_bl_proto.h
 * @brief	CDC protocol version 1 (bootloader.md, section 7): framing, commands, flash session.
 */
#ifndef FAC_BL_PROTO_H
#define FAC_BL_PROTO_H

#include <stdbool.h>
#include <stdint.h>

#define FAC_BL_SOF				0xA5
#define FAC_BL_REPLY_FLAG		0x80	// reply cmd = request cmd | 0x80
#define FAC_BL_MAX_PAYLOAD		1040U
#define FAC_BL_MAX_WRITE		1024U
#define FAC_BL_FRAME_OVERHEAD	9U		// SOF, cmd, seq, len (2), CRC (4)
#define FAC_BL_INFO_SIZE		44U

/* Commands */
enum {
	FAC_BL_CMD_INFO = 0x01,
	FAC_BL_CMD_ERASE = 0x02,
	FAC_BL_CMD_WRITE = 0x03,
	FAC_BL_CMD_VERIFY = 0x04,
	FAC_BL_CMD_BOOT = 0x05,
	FAC_BL_CMD_NAK = 0xFF,
};

/* Status, first byte of every reply payload */
enum {
	FAC_BL_OK = 0x00,
	FAC_BL_BAD_CRC = 0x01,
	FAC_BL_BAD_LENGTH = 0x02,
	FAC_BL_UNKNOWN_COMMAND = 0x03,
	FAC_BL_NOT_A_FRAME = 0x04,
	FAC_BL_OUT_OF_RANGE = 0x10,
	FAC_BL_MISALIGNED = 0x11,
	FAC_BL_NOT_ERASED = 0x12,
	FAC_BL_BAD_STATE = 0x13,
	FAC_BL_ERASE_FAILED = 0x20,
	FAC_BL_WRITE_FAILED = 0x21,
	FAC_BL_CRC_MISMATCH = 0x30,
	FAC_BL_BAD_HEADER = 0x31,
	FAC_BL_BAD_VECTORS = 0x32,
	FAC_BL_NO_VALID_APP = 0x33,
};

/* idle_return: entered on request with a valid application, so 60 s without packets go back to it */
void FAC_BL_proto_init(bool idle_return, uint32_t now);
void FAC_BL_proto_byte(uint8_t byte, uint32_t now);	// one received byte
void FAC_BL_proto_poll(uint32_t now);				// timeouts, pending reset

#endif /* FAC_BL_PROTO_H */
