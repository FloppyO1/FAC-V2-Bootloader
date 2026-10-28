/**
 * @file	fac_bl_flash.h
 * @brief	Flash erase/write with read back, returning protocol status codes.
 */
#ifndef FAC_BL_FLASH_H
#define FAC_BL_FLASH_H

#include <stdint.h>
#include "Bootloader/fac_boot_shared.h"

/* Erases count pages from address: FAC_BL_OK or FAC_BL_ERASE_FAILED */
uint8_t FAC_BL_flash_erase(uint32_t address, uint32_t count);

/* Writes length bytes (multiple of 2): FAC_BL_OK, _NOT_ERASED or _WRITE_FAILED.
 * Data already in flash is accepted as written, so a repeated packet answers OK. */
uint8_t FAC_BL_flash_write(uint32_t address, const uint8_t *data, uint32_t length);

/* Erases the record page and writes the record: FAC_BL_OK, _ERASE_FAILED or _WRITE_FAILED */
uint8_t FAC_BL_flash_write_record(const FAC_boot_record_t *record);

#endif /* FAC_BL_FLASH_H */
