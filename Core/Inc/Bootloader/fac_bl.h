/**
 * @file	fac_bl.h
 * @brief	Entry points of the F.A.C. V2 bootloader, called from the CubeMX generated code.
 */
#ifndef FAC_BL_H
#define FAC_BL_H

#include <stdbool.h>
#include <stdint.h>

void FAC_BL_fast_path(void);	// main(), before HAL_Init: starts a valid application and never returns
void FAC_BL_early_init(void);	// after SystemClock_Config
void FAC_BL_init(void);			// after the peripherals
void FAC_BL_loop(void);			// main loop

/* USB CDC receive callback (interrupt): true when there is room for another packet */
bool FAC_BL_usb_received(const uint8_t *data, uint32_t length);

/* LED: blink once more for a packet handled while writing */
void FAC_BL_led_activity(uint32_t now);

#endif /* FAC_BL_H */
