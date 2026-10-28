#include "test.h"
#include "helpers.h"
#include "sim_port.h"
#include "Bootloader/fac_bl.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"

#define SH	(FAC_BL_port_shared())

static void install_valid(void) {
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, true, NULL);
}

static void test_garbage_reinit(void) {
	sim.reset_flags = 0x04000000;
	sim_shared_garbage();
	FAC_BL_fast_path();	// erased app: stays
	CHECK_EQ(SH->magic, FAC_BOOT_MAGIC_SHARED);
	CHECK(FAC_BL_shared_is_valid());
	CHECK_EQ(SH->reset_flags, 0x04000000);
	CHECK_EQ(sim.reset_flags_calls, 1);
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_NO_APP);
	CHECK_EQ(sim.jump_calls, 0);
}

static void test_valid_jump(void) {
	install_valid();
	FAC_BL_fast_path();
	CHECK_EQ(sim.jump_calls, 1);
	CHECK_EQ(sim.jump_sp, 0x20004000);
	CHECK_EQ(sim.jump_pc, 0x08004101);
	CHECK_EQ(SH->boot_attempts, 1);
	CHECK(FAC_BL_shared_is_valid());
}

static void test_attempts(void) {
	install_valid();
	for (int i = 1; i <= 3; i++) {
		FAC_BL_fast_path();
		CHECK_EQ(sim.jump_calls, i);
		CHECK_EQ(SH->boot_attempts, i);
		CHECK(FAC_BL_shared_is_valid());
	}
	FAC_BL_fast_path();
	CHECK_EQ(sim.jump_calls, 3);
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_BOOT_FAILED);
	CHECK(FAC_BL_shared_is_valid());

	// power on: the count restarts
	sim.reset_flags = FAC_BL_RESET_FLAG_POWER_ON;
	FAC_BL_fast_path();
	CHECK_EQ(sim.jump_calls, 4);
	CHECK_EQ(SH->boot_attempts, 1);
}

static void test_stay_request(void) {
	install_valid();
	FAC_BL_shared_reset();
	SH->request = FAC_BOOT_REQUEST_STAY;
	FAC_BL_shared_seal();
	FAC_BL_fast_path();
	CHECK_EQ(sim.jump_calls, 0);
	CHECK_EQ(SH->request, 0);
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_REQUESTED);
	CHECK(FAC_BL_shared_is_valid());
	CHECK_EQ(SH->boot_attempts, 0);
	// next reset goes to the application
	FAC_BL_fast_path();
	CHECK_EQ(sim.jump_calls, 1);
}

static void test_debug_no_counting(void) {
	img_opts_t o = img_defaults();
	o.flags = FAC_BOOT_APP_FLAG_DEBUG;
	app_install(&o, APP_SIZE, false, NULL);
	for (int i = 1; i <= 10; i++) {
		FAC_BL_fast_path();
		CHECK_EQ(sim.jump_calls, i);
		CHECK_EQ(SH->boot_attempts, 0);
	}
	CHECK(FAC_BL_shared_is_valid());
}

static void test_invalid_app_reasons(void) {
	// erased
	FAC_BL_fast_path();
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_NO_APP);
	// crc mismatch
	install_valid();
	app_poke32(0x900, 0);
	FAC_BL_fast_path();
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_CRC_MISMATCH);
	// bad header
	sim_reset();
	img_opts_t o = img_defaults();
	o.hw_id = 1;
	app_install(&o, APP_SIZE, true, NULL);
	FAC_BL_fast_path();
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_BAD_HEADER);
	// bad vectors
	sim_reset();
	o = img_defaults();
	o.entry = 0x08004100;
	app_install(&o, APP_SIZE, true, NULL);
	FAC_BL_fast_path();
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_BAD_VECTORS);
	// no record
	sim_reset();
	o = img_defaults();
	app_install(&o, APP_SIZE, false, NULL);
	FAC_BL_fast_path();
	CHECK_EQ(SH->stay_reason, FAC_BOOT_STAY_NO_APP);
	CHECK_EQ(sim.jump_calls, 0);
	CHECK(FAC_BL_shared_is_valid());
}

static void test_early_init(void) {
	FAC_BL_early_init();
	CHECK_EQ(sim.early_init_calls, 1);
}

int main(void) {
	RUN(test_garbage_reinit);
	RUN(test_valid_jump);
	RUN(test_attempts);
	RUN(test_stay_request);
	RUN(test_debug_no_counting);
	RUN(test_invalid_app_reasons);
	RUN(test_early_init);
	return TEST_FINISH();
}
