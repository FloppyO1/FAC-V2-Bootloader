/**
 * @file	fac_bl_port.h
 * @brief	Every hardware access of the bootloader logic.
 * @note	fac_bl_port_stm32.c implements it on the board; the host unit tests (tests/) implement
 * 			it on a simulated flash, so the logic is tested without the board.
 */
#ifndef FAC_BL_PORT_H
#define FAC_BL_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "Bootloader/fac_boot_shared.h"

/* CRC peripheral: loads init (not reflected), feeds the words bit-reversed, returns the bit-reversed
 * result (the zlib running value) and turns the peripheral off again */
uint32_t FAC_BL_port_crc_words(uint32_t init, const uint32_t *words, uint32_t count);

/* Memory */
const uint8_t *FAC_BL_port_flash(uint32_t address);	// readable pointer to a flash address
volatile FAC_boot_shared_t *FAC_BL_port_shared(void);
const uint8_t *FAC_BL_port_uid(void);				// 12 bytes
uint32_t FAC_BL_port_take_reset_flags(void);		// RCC->CSR, then clears its flags

/* Flash programming, called between unlock and lock */
void FAC_BL_port_flash_unlock(void);
void FAC_BL_port_flash_lock(void);
bool FAC_BL_port_flash_erase_page(uint32_t address);
bool FAC_BL_port_flash_program16(uint32_t address, uint16_t value);

/* Board */
void FAC_BL_port_early_init(void);					// clock recovery, USB reconnect delay
uint32_t FAC_BL_port_millis(void);
void FAC_BL_port_led(bool on);
void FAC_BL_port_watchdog_kick(void);
bool FAC_BL_port_usb_send(const uint8_t *data, uint16_t length);	// false while busy
void FAC_BL_port_usb_rearm(void);					// accept the next USB packet
void FAC_BL_port_reset(void);						// system reset (returns only on the host)
void FAC_BL_port_jump(uint32_t stack, uint32_t entry);	// start the application (returns only on the host)

#endif /* FAC_BL_PORT_H */
