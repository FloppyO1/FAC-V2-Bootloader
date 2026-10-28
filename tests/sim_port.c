/* Simulated hardware: flash, CRC unit, shared RAM, USB, clock */
#include "sim_port.h"
#include "test.h"
#include <string.h>

sim_t sim;
int t_fail, t_run, t_run_failed;

static FAC_boot_shared_t shared_ram __attribute__((aligned(32)));

uint32_t sim_reflect32(uint32_t value) {
	uint32_t r = 0;
	for (int i = 0; i < 32; i++) {
		r = (r << 1) | (value & 1U);
		value >>= 1;
	}
	return r;
}

void sim_shared_garbage(void) {
	uint32_t *w = (uint32_t *) &shared_ram;
	for (unsigned i = 0; i < sizeof shared_ram / 4; i++)
		w[i] = (0xDEADBEEFU * (i + 1U)) ^ 0x5A5A1234U;
}

void sim_reset(void) {
	memset(&sim, 0, sizeof sim);
	memset(sim.flash, 0xFF, sizeof sim.flash);
	sim.fail_erase_page = SIM_NONE;
	sim.fail_program_addr = SIM_NONE;
	sim.corrupt_program_addr = SIM_NONE;
	for (int i = 0; i < 12; i++)
		sim.uid[i] = (uint8_t) (0xA0 + i);
	sim.now = 1000;
	sim_shared_garbage();
}

void sim_flash_load(uint32_t address, const void *data, uint32_t length) {
	memcpy((uint8_t *) sim.flash + (address - SIM_FLASH_BASE), data, length);
}

void sim_tx_clear(void) {
	sim.tx_len = 0;
}

uint32_t FAC_BL_port_crc_words(uint32_t init, const uint32_t *words, uint32_t count) {
	sim.crc_calls++;
	uint32_t r = init;
	for (uint32_t i = 0; i < count; i++) {
		r ^= sim_reflect32(words[i]);
		for (int b = 0; b < 32; b++)
			r = (r & 0x80000000U) ? (r << 1) ^ 0x04C11DB7U : (r << 1);
	}
	return sim_reflect32(r);
}

const uint8_t *FAC_BL_port_flash(uint32_t address) {
	return (const uint8_t *) sim.flash + (address - SIM_FLASH_BASE);
}

volatile FAC_boot_shared_t *FAC_BL_port_shared(void) {
	return &shared_ram;
}

const uint8_t *FAC_BL_port_uid(void) {
	return sim.uid;
}

uint32_t FAC_BL_port_take_reset_flags(void) {
	sim.reset_flags_calls++;
	return sim.reset_flags;
}

void FAC_BL_port_flash_unlock(void) {
	sim.unlocked = true;
}

void FAC_BL_port_flash_lock(void) {
	sim.unlocked = false;
}

bool FAC_BL_port_flash_erase_page(uint32_t address) {
	if (!sim.unlocked || (address & (SIM_PAGE - 1)) != 0 || address < SIM_FLASH_BASE
			|| address >= SIM_FLASH_BASE + SIM_FLASH_SIZE)
		return false;
	uint32_t page = (address - SIM_FLASH_BASE) / SIM_PAGE;
	if (sim.erase_log_n < SIM_LOG_MAX)
		sim.erase_log[sim.erase_log_n++] = address;
	if (page == sim.fail_erase_page)
		return false;
	sim.erase_count++;
	memset((uint8_t *) sim.flash + page * SIM_PAGE, 0xFF, SIM_PAGE);
	return true;
}

bool FAC_BL_port_flash_program16(uint32_t address, uint16_t value) {
	if (!sim.unlocked || (address & 1U) != 0 || address < SIM_FLASH_BASE
			|| address >= SIM_FLASH_BASE + SIM_FLASH_SIZE)
		return false;
	if (address == sim.fail_program_addr)
		return false;
	uint8_t *p = (uint8_t *) sim.flash + (address - SIM_FLASH_BASE);
	uint16_t current = (uint16_t) (p[0] | (p[1] << 8));
	if (current != 0xFFFF && value != 0)
		return false;	// real flash cannot program a written halfword
	if (address == sim.corrupt_program_addr)
		value ^= 0x0100;
	p[0] = (uint8_t) value;
	p[1] = (uint8_t) (value >> 8);
	sim.program_count++;
	return true;
}

void FAC_BL_port_early_init(void) {
	sim.early_init_calls++;
}

uint32_t FAC_BL_port_millis(void) {
	if (sim.busy_forever || sim.busy_calls > 0)
		sim.now++;	// lets busy waits time out
	return sim.now;
}

void FAC_BL_port_led(bool on) {
	sim.led = on;
	sim.led_toggles++;
}

void FAC_BL_port_watchdog_kick(void) {
	sim.wd_kicks++;
}

bool FAC_BL_port_usb_send(const uint8_t *data, uint16_t length) {
	if (sim.busy_forever)
		return false;
	if (sim.busy_calls > 0) {
		sim.busy_calls--;
		return false;
	}
	if (sim.tx_len + length <= SIM_TX_MAX) {
		memcpy(sim.tx + sim.tx_len, data, length);
		sim.tx_len += length;
	}
	sim.tx_calls++;
	return true;
}

void FAC_BL_port_usb_rearm(void) {
	sim.rearm_calls++;
}

void FAC_BL_port_reset(void) {
	sim.reset_calls++;
}

void FAC_BL_port_jump(uint32_t stack, uint32_t entry) {
	sim.jump_calls++;
	sim.jump_sp = stack;
	sim.jump_pc = entry;
}
