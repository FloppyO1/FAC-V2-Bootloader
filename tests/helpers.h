/* Test helpers: reference CRC, application images, frames, replies */
#ifndef HELPERS_H
#define HELPERS_H

#include <stdbool.h>
#include <stdint.h>
#include "Bootloader/fac_boot_shared.h"

#define APP_BASE	FAC_BOOT_APP_BASE
#define REC_ADDR	FAC_BOOT_RECORD_ADDR
#define APP_SIZE	4096U

/* Reference zlib CRC-32, bitwise */
uint32_t ref_crc32(uint32_t crc, const void *data, uint32_t length);

/* Application image */
typedef struct {
	uint32_t sp, entry;
	uint32_t magic;
	uint8_t header_version, hw_id;
	uint16_t fw_version;
	uint32_t link_address;
	uint32_t flags;
} img_opts_t;

img_opts_t img_defaults(void);
void img_build(uint8_t *img, uint32_t size, const img_opts_t *opts);
void rec_seal(FAC_boot_record_t *rec);						// sets record_crc
FAC_boot_record_t rec_make(const uint8_t *img, uint32_t size, uint16_t fw_version);	// sealed
void rec_write(const FAC_boot_record_t *rec);				// raw load at the record address
/* Loads an image (and its record) into the simulated flash; returns the host copy of the image */
uint8_t *app_install(const img_opts_t *opts, uint32_t size, bool with_record, FAC_boot_record_t *rec);
void app_poke32(uint32_t offset, uint32_t value);			// raw change of an app word

/* Frames */
uint32_t frame_build(uint8_t *out, uint8_t cmd, uint8_t seq, const void *payload, uint16_t length);

typedef struct {
	uint8_t cmd, seq;
	uint16_t len;
	uint8_t payload[64];
} reply_t;

/* Parses sim.tx; returns the number of frames, -1 if one is malformed (SOF, CRC or length) */
int replies_parse(reply_t *out, int max);
uint32_t rd32(const uint8_t *p);
uint16_t rd16(const uint8_t *p);

/* Driving the bootloader through the USB ring */
void bl_enter(uint8_t stay_reason);							// shared block + FAC_BL_init
void bl_feed(const void *data, uint32_t length);			// USB packets of 64 bytes, no loop
void bl_run(void);											// one FAC_BL_loop
void bl_advance(uint32_t ms);								// fake time, then one loop
/* Sends one request, runs the loop, returns the number of replies (first in *r) */
int rq(uint8_t cmd, uint8_t seq, const void *payload, uint16_t length, reply_t *r);

#endif /* HELPERS_H */
