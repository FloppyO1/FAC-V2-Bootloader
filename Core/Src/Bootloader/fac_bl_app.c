/**
 * @file	fac_bl_app.c
 * @brief	Application checks (levels 3 and 4 of bootloader.md, section 6) and the shared RAM block.
 */
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"
#include "Bootloader/fac_bl_crc.h"
#include "Bootloader/fac_bl_port.h"
#include <stddef.h>
#include <string.h>

#define ERASED_WORD		0xFFFFFFFFUL

static uint32_t read32(uint32_t address) {
	uint32_t value;
	memcpy(&value, FAC_BL_port_flash(address), sizeof value);
	return value;
}

bool FAC_BL_app_read_header(FAC_boot_app_header_t *header) {
	memcpy(header, FAC_BL_port_flash(FAC_BOOT_APP_HEADER_ADDR), sizeof *header);
	return header->magic == FAC_BOOT_MAGIC_APP;
}

bool FAC_BL_app_read_record(FAC_boot_record_t *record) {
	memcpy(record, FAC_BL_port_flash(FAC_BOOT_RECORD_ADDR), sizeof *record);
	bool valid = record->magic == FAC_BOOT_MAGIC_RECORD
			&& record->record_crc == FAC_BL_crc32(0, record, offsetof(FAC_boot_record_t, record_crc))
			&& record->app_size != 0
			&& record->app_size <= FAC_BOOT_APP_MAX_SIZE
			&& (record->app_size & 3U) == 0;
	if (!valid)
		memset(record, 0, sizeof *record);
	return valid;
}

/* Header for this board and address, vectors plausible */
uint8_t FAC_BL_app_check_image(uint32_t size) {
	FAC_boot_app_header_t header;
	if (size < FAC_BOOT_VECTORS_SIZE + sizeof header || !FAC_BL_app_read_header(&header)
			|| header.header_version != FAC_BL_HEADER_VERSION || header.hw_id != FAC_BOOT_HW_ID
			|| header.link_address != FAC_BOOT_APP_BASE)
		return FAC_BOOT_STAY_BAD_HEADER;

	uint32_t stack = read32(FAC_BOOT_APP_BASE);
	uint32_t entry = read32(FAC_BOOT_APP_BASE + 4U);
	if (stack <= FAC_BL_APP_RAM_START || stack > FAC_BL_APP_RAM_END || (stack & 3U) != 0
			|| (entry & 1U) == 0	// Thumb
			|| entry < FAC_BOOT_APP_BASE || entry >= FAC_BOOT_APP_BASE + FAC_BOOT_APP_MAX_SIZE)
		return FAC_BOOT_STAY_BAD_VECTORS;
	return FAC_BOOT_STAY_NONE;
}

void FAC_BL_app_check(FAC_BL_app_status_t *status) {
	memset(status, 0, sizeof *status);
	status->record_valid = FAC_BL_app_read_record(&status->record);

	FAC_boot_app_header_t header;
	if (FAC_BL_app_read_header(&header)) {
		status->fw_version = header.fw_version;
		status->debug = (header.flags & FAC_BOOT_APP_FLAG_DEBUG) != 0;
	}

	if (read32(FAC_BOOT_APP_BASE) == ERASED_WORD) {	// nothing installed
		status->reason = FAC_BOOT_STAY_NO_APP;
		status->state = FAC_BL_APP_STATE_NONE;
		return;
	}

	status->reason = FAC_BL_app_check_image(FAC_BOOT_APP_MAX_SIZE);
	if (status->reason == FAC_BOOT_STAY_NONE && !status->debug) {	// Debug builds start without record
		if (!status->record_valid)
			status->reason = FAC_BOOT_STAY_NO_APP;
		else if (status->record.fw_version != status->fw_version
				|| FAC_BL_crc32(0, FAC_BL_port_flash(FAC_BOOT_APP_BASE), status->record.app_size) != status->record.app_crc)
			status->reason = FAC_BOOT_STAY_CRC_MISMATCH;	// written without the tool, or altered
	}

	if (status->reason != FAC_BOOT_STAY_NONE)
		status->state = FAC_BL_APP_STATE_INVALID;
	else
		status->state = status->debug ? FAC_BL_APP_STATE_DEBUG : FAC_BL_APP_STATE_VALID;
}

/* Check word: ~(word0 ^ word1 ^ word2 ^ word3) */
static uint32_t shared_check(volatile const FAC_boot_shared_t *shared) {
	volatile const uint32_t *word = (volatile const uint32_t *) shared;
	return ~(word[0] ^ word[1] ^ word[2] ^ word[3]);
}

bool FAC_BL_shared_is_valid(void) {
	volatile FAC_boot_shared_t *shared = FAC_BL_port_shared();
	return shared->magic == FAC_BOOT_MAGIC_SHARED && shared->check == shared_check(shared);
}

void FAC_BL_shared_reset(void) {
	volatile uint32_t *word = (volatile uint32_t *) FAC_BL_port_shared();
	for (uint32_t i = 0; i < sizeof(FAC_boot_shared_t) / 4U; i++)
		word[i] = 0;
	FAC_BL_port_shared()->magic = FAC_BOOT_MAGIC_SHARED;
	FAC_BL_shared_seal();
}

void FAC_BL_shared_seal(void) {
	volatile FAC_boot_shared_t *shared = FAC_BL_port_shared();
	shared->check = shared_check(shared);
}
