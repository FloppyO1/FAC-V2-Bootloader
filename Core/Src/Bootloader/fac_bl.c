/**
 * @file	fac_bl.c
 * @brief	Start up decision (bootloader.md, section 6.1), USB receive buffer, LED and main loop.
 */
#include "Bootloader/fac_bl.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"
#include "Bootloader/fac_bl_port.h"
#include "Bootloader/fac_bl_proto.h"

#define RING_MASK	(FAC_BL_RX_RING_SIZE - 1U)

/* USB receive ring: written by the USB interrupt, read by the main loop */
static uint8_t ring[FAC_BL_RX_RING_SIZE];
static volatile uint16_t ring_head;
static volatile uint16_t ring_tail;
static volatile bool rx_paused;	// USB packet not re-armed until there is room

static struct {
	bool on;
	bool activity;
	uint32_t activity_ms;
} led;

static uint16_t ring_free(void) {
	return (uint16_t) (FAC_BL_RX_RING_SIZE - 1U - ((ring_head - ring_tail) & RING_MASK));
}

/* Stays in the bootloader for the given reason */
static void stay(uint8_t reason) {
	FAC_BL_port_shared()->stay_reason = reason;
	FAC_BL_shared_seal();
}

void FAC_BL_fast_path(void) {
	// runs at 8 MHz on HSI, before any clock or peripheral setup
	if (!FAC_BL_shared_is_valid())
		FAC_BL_shared_reset();	// power on: random RAM content
	volatile FAC_boot_shared_t *shared = FAC_BL_port_shared();
	shared->reset_flags = FAC_BL_port_take_reset_flags();
	if (shared->reset_flags & FAC_BL_RESET_FLAG_POWER_ON)
		shared->boot_attempts = 0;

	if (shared->request == FAC_BOOT_REQUEST_STAY) {	// asked by the application (tool command)
		shared->request = 0;
		stay(FAC_BOOT_STAY_REQUESTED);
		return;
	}

	FAC_BL_app_status_t app;
	FAC_BL_app_check(&app);
	if (app.reason != FAC_BOOT_STAY_NONE) {
		stay(app.reason);
		return;
	}

	if (!app.debug) {	// boot confirmation, cleared by the application after 1 s of main loop
		if (shared->boot_attempts < 0xFF)
			shared->boot_attempts++;
		if (shared->boot_attempts > FAC_BL_MAX_BOOT_ATTEMPTS) {
			stay(FAC_BOOT_STAY_BOOT_FAILED);
			return;
		}
	}
	FAC_BL_shared_seal();

	const uint32_t *vectors = (const uint32_t *) FAC_BL_port_flash(FAC_BOOT_APP_BASE);
	FAC_BL_port_jump(vectors[0], vectors[1]);
}

void FAC_BL_early_init(void) {
	FAC_BL_port_early_init();
}

void FAC_BL_init(void) {
	// ring and LED start zeroed (.bss): the USB interrupt is already running here, so no reset

	// the idle return only applies when entered on request with a valid application
	bool idle_return = false;
	if (FAC_BL_port_shared()->stay_reason == FAC_BOOT_STAY_REQUESTED) {
		FAC_BL_app_status_t app;
		FAC_BL_app_check(&app);
		idle_return = app.reason == FAC_BOOT_STAY_NONE;
	}
	FAC_BL_proto_init(idle_return, FAC_BL_port_millis());
}

bool FAC_BL_usb_received(const uint8_t *data, uint32_t length) {
	uint16_t head = ring_head;
	for (uint32_t i = 0; i < length && ring_free() > 0; i++) {
		ring[head] = data[i];
		head = (uint16_t) ((head + 1U) & RING_MASK);
		ring_head = head;
	}
	if (ring_free() >= FAC_BL_USB_PACKET_SIZE)
		return true;
	rx_paused = true;	// the main loop re-arms the endpoint once it has read enough
	return false;
}

void FAC_BL_led_activity(uint32_t now) {
	led.on = !led.on;
	FAC_BL_port_led(led.on);
	led.activity = true;
	led.activity_ms = now;
}

/* Two short blinks per second while waiting */
static void led_update(uint32_t now) {
	if (led.activity && now - led.activity_ms < FAC_BL_LED_ACTIVITY_MS)
		return;
	led.activity = false;
	uint32_t t = now % 1000U;
	bool on = t < 60U || (t >= 180U && t < 240U);
	if (on != led.on) {
		led.on = on;
		FAC_BL_port_led(on);
	}
}

void FAC_BL_loop(void) {
	FAC_BL_port_watchdog_kick();

	while (ring_tail != ring_head) {
		uint8_t byte = ring[ring_tail];
		ring_tail = (uint16_t) ((ring_tail + 1U) & RING_MASK);
		FAC_BL_proto_byte(byte, FAC_BL_port_millis());
	}
	if (rx_paused && ring_free() >= FAC_BL_USB_PACKET_SIZE) {
		rx_paused = false;
		FAC_BL_port_usb_rearm();
	}

	uint32_t now = FAC_BL_port_millis();
	FAC_BL_proto_poll(now);
	led_update(now);
}
