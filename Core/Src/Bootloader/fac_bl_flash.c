/**
 * @file	fac_bl_flash.c
 * @brief	Flash erase/write with read back (level 2 of bootloader.md, section 6).
 */
#include "Bootloader/fac_bl_flash.h"
#include "Bootloader/fac_bl_port.h"
#include "Bootloader/fac_bl_proto.h"
#include <string.h>

#define ERASED_HALFWORD	0xFFFFU

static uint16_t flash16(uint32_t address) {
	const uint8_t *p = FAC_BL_port_flash(address);
	return (uint16_t) (p[0] | (p[1] << 8));
}

static uint16_t data16(const uint8_t *data, uint32_t offset) {
	return (uint16_t) (data[offset] | (data[offset + 1] << 8));
}

uint8_t FAC_BL_flash_erase(uint32_t address, uint32_t count) {
	uint8_t status = FAC_BL_OK;
	FAC_BL_port_flash_unlock();
	for (uint32_t page = 0; page < count && status == FAC_BL_OK; page++) {
		FAC_BL_port_watchdog_kick();
		if (!FAC_BL_port_flash_erase_page(address + page * FAC_BOOT_PAGE_SIZE))
			status = FAC_BL_ERASE_FAILED;
	}
	FAC_BL_port_flash_lock();
	return status;
}

uint8_t FAC_BL_flash_write(uint32_t address, const uint8_t *data, uint32_t length) {
	// nothing is written unless every halfword is either erased or already right
	for (uint32_t i = 0; i < length; i += 2) {
		uint16_t current = flash16(address + i);
		if (current != data16(data, i) && current != ERASED_HALFWORD)
			return FAC_BL_NOT_ERASED;
	}

	uint8_t status = FAC_BL_OK;
	FAC_BL_port_flash_unlock();
	for (uint32_t i = 0; i < length && status == FAC_BL_OK; i += 2) {
		uint16_t value = data16(data, i);
		if (flash16(address + i) != value && !FAC_BL_port_flash_program16(address + i, value))
			status = FAC_BL_WRITE_FAILED;
	}
	FAC_BL_port_flash_lock();

	if (status == FAC_BL_OK && memcmp(FAC_BL_port_flash(address), data, length) != 0)
		status = FAC_BL_WRITE_FAILED;	// read back differs
	return status;
}

uint8_t FAC_BL_flash_write_record(const FAC_boot_record_t *record) {
	uint8_t status = FAC_BL_flash_erase(FAC_BOOT_RECORD_ADDR, 1);
	if (status == FAC_BL_OK)
		status = FAC_BL_flash_write(FAC_BOOT_RECORD_ADDR, (const uint8_t *) record, sizeof *record);
	return status;
}
