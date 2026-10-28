/**
 * @file	fac_bl_info.c
 * @brief	Bootloader information block at 0x080000C0 (bootloader.md, section 4.1).
 * @note	Read by the application (command 8) and by the FAC Tool DFU recovery to recognise
 * 			a bootloader file. The linker script places it right after the vectors.
 */
#include "Bootloader/fac_boot_shared.h"
#include "Bootloader/fac_bl_config.h"

__attribute__((section(".fac_bl_info"), used))
const FAC_boot_bl_info_t fac_bl_info = {
	.magic = FAC_BOOT_MAGIC_BL_INFO,
	.info_version = FAC_BL_INFO_VERSION,
	.hw_id = FAC_BOOT_HW_ID,
	.bl_version = FAC_BL_VERSION,
	.bl_size_max = FAC_BOOT_BL_SIZE_MAX,
	.app_base = FAC_BOOT_APP_BASE,
	.protocol_version = FAC_BL_PROTOCOL_VERSION,
	.reserved = { 0, 0, 0 },
};
