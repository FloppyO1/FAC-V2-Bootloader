/**
 * @file	fac_bl_port_stm32.c
 * @brief	Hardware side of the bootloader on the STM32F072 (see fac_bl_port.h).
 */
#include "Bootloader/fac_bl_port.h"
#include "Bootloader/fac_bl_config.h"
#include "main.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

extern USBD_HandleTypeDef hUsbDeviceFS;	// usb_device.c
extern uint8_t UserRxBufferFS[];		// usbd_cdc_if.c

#define IWDG_KEY_RELOAD	0xAAAAU
#define CRC32_POLY		0x04C11DB7UL

_Static_assert(FAC_BL_RESET_FLAG_POWER_ON == RCC_CSR_PORRSTF, "power on reset flag");

uint32_t FAC_BL_port_crc_words(uint32_t init, const uint32_t *words, uint32_t count) {
	RCC->AHBENR |= RCC_AHBENR_CRCEN;
	(void) RCC->AHBENR;	// clock running before the first access
	CRC->POL = CRC32_POLY;
	CRC->INIT = init;
	CRC->CR = CRC_CR_REV_OUT | CRC_CR_REV_IN | CRC_CR_RESET;	// 32 bit poly, words bit-reversed in and out
	while (count--)
		CRC->DR = *words++;
	uint32_t result = CRC->DR;
	RCC->AHBENR &= ~RCC_AHBENR_CRCEN;	// the application finds it off, as after reset
	return result;
}

const uint8_t *FAC_BL_port_flash(uint32_t address) {
	return (const uint8_t *) address;
}

volatile FAC_boot_shared_t *FAC_BL_port_shared(void) {
	return (volatile FAC_boot_shared_t *) FAC_BOOT_SHARED_ADDR;
}

const uint8_t *FAC_BL_port_uid(void) {
	return (const uint8_t *) UID_BASE;
}

uint32_t FAC_BL_port_take_reset_flags(void) {
	uint32_t flags = RCC->CSR;
	RCC->CSR |= RCC_CSR_RMVF;	// otherwise PORRSTF stays set until power off
	return flags;
}

/* Flash with registers (RM0091 section 3): smaller than the HAL driver */
void FAC_BL_port_flash_unlock(void) {
	if (FLASH->CR & FLASH_CR_LOCK) {
		FLASH->KEYR = FLASH_KEY1;
		FLASH->KEYR = FLASH_KEY2;
	}
}

void FAC_BL_port_flash_lock(void) {
	FLASH->CR |= FLASH_CR_LOCK;
}

/* Waits for the operation, true if it ended without errors */
static bool flash_done(void) {
	while (FLASH->SR & FLASH_SR_BSY) {
	}
	bool ok = (FLASH->SR & (FLASH_SR_PGERR | FLASH_SR_WRPERR)) == 0;
	FLASH->SR = FLASH_SR_EOP | FLASH_SR_PGERR | FLASH_SR_WRPERR;	// write 1 to clear
	return ok;
}

bool FAC_BL_port_flash_erase_page(uint32_t address) {
	FLASH->CR |= FLASH_CR_PER;
	FLASH->AR = address;
	FLASH->CR |= FLASH_CR_STRT;
	bool ok = flash_done();
	FLASH->CR &= ~FLASH_CR_PER;
	return ok;
}

bool FAC_BL_port_flash_program16(uint32_t address, uint16_t value) {
	FLASH->CR |= FLASH_CR_PG;
	*(volatile uint16_t *) address = value;
	bool ok = flash_done();
	FLASH->CR &= ~FLASH_CR_PG;
	return ok;
}

void FAC_BL_port_early_init(void) {
	// HSI48 trimmed on the USB start of frame (CRS), for the USB clock accuracy
	LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_CRS);
	LL_CRS_ConfigSynchronization(LL_CRS_HSI48CALIBRATION_DEFAULT, LL_CRS_ERRORLIMIT_DEFAULT,
			LL_CRS_RELOADVALUE_DEFAULT, LL_CRS_SYNC_DIV_1 | LL_CRS_SYNC_SOURCE_USB | LL_CRS_SYNC_POLARITY_RISING);
	LL_CRS_EnableFreqErrorCounter();
	LL_CRS_EnableAutoTrimming();

	HAL_Delay(FAC_BL_USB_RECONNECT_DELAY_MS);	// D+ pull-up still off: the host sees the device leave
}

uint32_t FAC_BL_port_millis(void) {
	return HAL_GetTick();
}

void FAC_BL_port_led(bool on) {
	if (on)
		LL_GPIO_SetOutputPin(LED_GPIO_Port, LED_Pin);
	else
		LL_GPIO_ResetOutputPin(LED_GPIO_Port, LED_Pin);
}

void FAC_BL_port_watchdog_kick(void) {
	IWDG->KR = IWDG_KEY_RELOAD;	// harmless when the watchdog is off
}

bool FAC_BL_port_usb_send(const uint8_t *data, uint16_t length) {
	if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED || hUsbDeviceFS.pClassData == NULL)
		return false;
	return CDC_Transmit_FS((uint8_t *) data, length) == USBD_OK;
}

void FAC_BL_port_usb_rearm(void) {
	NVIC_DisableIRQ(USB_IRQn);	// the USB interrupt touches the same endpoint state
	if (hUsbDeviceFS.pClassData != NULL) {
		USBD_CDC_SetRxBuffer(&hUsbDeviceFS, UserRxBufferFS);
		USBD_CDC_ReceivePacket(&hUsbDeviceFS);
	}
	NVIC_EnableIRQ(USB_IRQn);
}

void FAC_BL_port_reset(void) {
	NVIC_SystemReset();
}

void FAC_BL_port_jump(uint32_t stack, uint32_t entry) {
	// nothing was set up yet (no clocks, no interrupts): the application starts as after a reset
	__asm volatile (
		"msr msp, %0\n"
		"bx %1\n"
		: : "r" (stack), "r" (entry) : "memory");
	while (1) {
	}
}
