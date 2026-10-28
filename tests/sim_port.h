/* Simulated hardware behind FAC_BL_port_* for the host tests */
#ifndef SIM_PORT_H
#define SIM_PORT_H

#include <stdbool.h>
#include <stdint.h>
#include "Bootloader/fac_bl_port.h"

#define SIM_FLASH_BASE	0x08000000UL
#define SIM_FLASH_SIZE	0x20000UL
#define SIM_PAGE		2048UL
#define SIM_LOG_MAX		256
#define SIM_TX_MAX		16384
#define SIM_NONE		0xFFFFFFFFUL

typedef struct {
	/* flash */
	uint32_t flash[SIM_FLASH_SIZE / 4];
	bool unlocked;
	uint32_t erase_count, program_count;
	uint32_t erase_log[SIM_LOG_MAX];		// page addresses in call order
	uint32_t erase_log_n;
	uint32_t fail_erase_page;				// page index (from flash base) that fails, SIM_NONE = none
	uint32_t fail_program_addr;				// address whose program fails, SIM_NONE = none
	uint32_t corrupt_program_addr;			// address where a different value is written, SIM_NONE = none
	/* crc unit */
	uint32_t crc_calls;
	/* misc */
	uint8_t uid[12];
	uint32_t reset_flags;
	uint32_t reset_flags_calls;
	uint32_t now;
	uint32_t early_init_calls, wd_kicks;
	bool led;
	uint32_t led_toggles;
	/* usb */
	uint8_t tx[SIM_TX_MAX];
	uint32_t tx_len, tx_calls;
	uint32_t busy_calls;					// next N sends return false
	bool busy_forever;
	uint32_t rearm_calls;
	/* reset and jump */
	uint32_t reset_calls, jump_calls, jump_sp, jump_pc;
} sim_t;

extern sim_t sim;

void sim_reset(void);					// everything back to power-on state, flash erased
void sim_shared_garbage(void);			// random looking RAM content
void sim_flash_load(uint32_t address, const void *data, uint32_t length);	// direct write, no flash rules
void sim_tx_clear(void);
uint32_t sim_reflect32(uint32_t value);

#endif /* SIM_PORT_H */
