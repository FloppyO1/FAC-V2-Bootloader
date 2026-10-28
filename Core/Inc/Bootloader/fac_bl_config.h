/**
 * @file	fac_bl_config.h
 * @brief	Bootloader version, timings and limits (bootloader.md, sections 6-7).
 */
#ifndef FAC_BL_CONFIG_H
#define FAC_BL_CONFIG_H

/* Bootloader version, encoded as major*10000 + minor*100 + patch like FIRMWARE_VERSION_VALUE */
#define FAC_BL_VERSION_MAJOR		1
#define FAC_BL_VERSION_MINOR		0
#define FAC_BL_VERSION_PATCH		0
#define FAC_BL_VERSION				(FAC_BL_VERSION_MAJOR * 10000 + FAC_BL_VERSION_MINOR * 100 + FAC_BL_VERSION_PATCH)

#define FAC_BL_PROTOCOL_VERSION		1
#define FAC_BL_INFO_VERSION			1		// layout of FAC_boot_bl_info_t
#define FAC_BL_HEADER_VERSION		1		// accepted FAC_boot_app_header_t layout

/* Boot confirmation: the application may start this many times without confirming */
#define FAC_BL_MAX_BOOT_ATTEMPTS	3

/* Application RAM, for the initial stack pointer check (first 256 bytes are reserved) */
#define FAC_BL_APP_RAM_START		0x20000100UL
#define FAC_BL_APP_RAM_END			0x20004000UL

/* RCC_CSR_PORRSTF of the STM32F072: power on or power down reset */
#define FAC_BL_RESET_FLAG_POWER_ON	(1UL << 27)

/* Timings, in milliseconds */
#define FAC_BL_IDLE_RETURN_MS		60000U	// back to the application when entered on request
#define FAC_BL_FRAME_GAP_MS			100U	// a pause this long drops a partial frame
#define FAC_BL_JUNK_SILENCE_MS		20U		// silence after stray bytes before the NOT_A_FRAME NAK
#define FAC_BL_BOOT_RESET_DELAY_MS	50U		// lets the BOOT reply leave before the reset
#define FAC_BL_USB_RECONNECT_DELAY_MS 50U	// D+ pull-up off this long, so the host sees a new device
#define FAC_BL_TX_TIMEOUT_MS		100U	// gives up a reply the host does not read
#define FAC_BL_LED_ACTIVITY_MS		500U	// LED follows the packets for this long after the last one

/* USB receive ring buffer, power of two */
#define FAC_BL_RX_RING_SIZE			2048U
#define FAC_BL_USB_PACKET_SIZE		64U

#endif /* FAC_BL_CONFIG_H */
