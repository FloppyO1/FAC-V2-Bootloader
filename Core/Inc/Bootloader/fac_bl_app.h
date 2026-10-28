/**
 * @file	fac_bl_app.h
 * @brief	Application checks (bootloader.md, section 6.1) and the shared RAM block.
 */
#ifndef FAC_BL_APP_H
#define FAC_BL_APP_H

#include <stdbool.h>
#include <stdint.h>
#include "Bootloader/fac_boot_shared.h"

/* app_state of the INFO reply */
enum {
	FAC_BL_APP_STATE_NONE = 0,		// application area erased
	FAC_BL_APP_STATE_VALID = 1,
	FAC_BL_APP_STATE_DEBUG = 2,		// Debug build, started without record
	FAC_BL_APP_STATE_INVALID = 3,
};

typedef struct {
	uint8_t reason;			// FAC_BOOT_STAY_NONE when the application can start
	uint8_t state;			// FAC_BL_APP_STATE_*
	bool debug;				// header has the DEBUG flag
	uint16_t fw_version;	// from the header, 0 if the header is missing
	bool record_valid;
	FAC_boot_record_t record;	// zeroed when not valid
} FAC_BL_app_status_t;

/* Full check, CRC included (skipped for Debug builds) */
void FAC_BL_app_check(FAC_BL_app_status_t *status);

/* Header and vectors of an image of the given size: FAC_BOOT_STAY_NONE, _BAD_HEADER or _BAD_VECTORS */
uint8_t FAC_BL_app_check_image(uint32_t size);

/* Header of the application (copy), false if its magic is missing */
bool FAC_BL_app_read_header(FAC_boot_app_header_t *header);

/* Record "application valid": copy, false if missing or corrupt */
bool FAC_BL_app_read_record(FAC_boot_record_t *record);

/* Shared RAM block */
bool FAC_BL_shared_is_valid(void);
void FAC_BL_shared_reset(void);		// zeroed with its magic
void FAC_BL_shared_seal(void);		// recomputes the check word after a change

#endif /* FAC_BL_APP_H */
