#include "test.h"
#include "helpers.h"
#include "sim_port.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"

static FAC_BL_app_status_t check(void) {
	FAC_BL_app_status_t s;
	FAC_BL_app_check(&s);
	return s;
}

static void test_erased(void) {
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NO_APP);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_NONE);
	CHECK(!s.record_valid);
}

static void test_valid(void) {
	img_opts_t o = img_defaults();
	FAC_boot_record_t rec;
	app_install(&o, APP_SIZE, true, &rec);
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NONE);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_VALID);
	CHECK_EQ(s.fw_version, 20100);
	CHECK(s.record_valid);
	CHECK(!s.debug);
	CHECK_EQ(s.record.app_size, APP_SIZE);
	CHECK_EQ(s.record.app_crc, rec.app_crc);
	CHECK_EQ(s.record.fw_version, 20100);
	CHECK_EQ(FAC_BL_app_check_image(APP_SIZE), FAC_BOOT_STAY_NONE);
}

static void test_no_record(void) {
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, false, NULL);
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NO_APP);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_INVALID);
	FAC_boot_record_t r;
	CHECK(!FAC_BL_app_read_record(&r));
}

static void test_bad_record(void) {
	img_opts_t o = img_defaults();
	FAC_boot_record_t rec, bad;
	uint8_t *img = app_install(&o, APP_SIZE, true, &rec);
	FAC_BL_app_status_t s;

	// corrupt record crc
	bad = rec;
	bad.record_crc ^= 1;
	rec_write(&bad);
	s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NO_APP);
	CHECK(!s.record_valid);

	// wrong magic with a good crc
	bad = rec;
	bad.magic = 0;
	rec_seal(&bad);
	rec_write(&bad);
	CHECK_EQ(check().reason, FAC_BOOT_STAY_NO_APP);

	// sizes: 0, not a multiple of 4, too big (all with a correct record crc)
	uint32_t sizes[] = { 0, 4094, 4097, 0x1B804, 0x20000 };
	for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
		bad = rec_make(img, APP_SIZE, 20100);
		bad.app_size = sizes[i];
		rec_seal(&bad);
		rec_write(&bad);
		s = check();
		CHECK_EQ(s.reason, FAC_BOOT_STAY_NO_APP);
		CHECK(!s.record_valid);
		CHECK(!FAC_BL_app_read_record(&bad));
	}
}

static void test_crc_mismatch(void) {
	img_opts_t o = img_defaults();
	FAC_boot_record_t rec;
	app_install(&o, APP_SIZE, true, &rec);
	app_poke32(0x800, 0x12345678);	// changed after the record was written
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_CRC_MISMATCH);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_INVALID);
}

static void test_record_fw_mismatch(void) {
	img_opts_t o = img_defaults();
	FAC_boot_record_t rec;
	app_install(&o, APP_SIZE, true, &rec);
	rec.fw_version = 20101;
	rec_seal(&rec);
	rec_write(&rec);
	CHECK_EQ(check().reason, FAC_BOOT_STAY_CRC_MISMATCH);
}

static void expect_header(img_opts_t o, uint8_t reason) {
	sim_reset();
	app_install(&o, APP_SIZE, true, NULL);
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, reason);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_INVALID);
}

static void test_bad_header(void) {
	img_opts_t o = img_defaults();
	o.magic = 0x12345678;
	expect_header(o, FAC_BOOT_STAY_BAD_HEADER);
	o = img_defaults();
	o.header_version = 2;
	expect_header(o, FAC_BOOT_STAY_BAD_HEADER);
	o = img_defaults();
	o.hw_id = 1;
	expect_header(o, FAC_BOOT_STAY_BAD_HEADER);
	o = img_defaults();
	o.link_address = 0x08000000;
	expect_header(o, FAC_BOOT_STAY_BAD_HEADER);
}

static void test_bad_vectors(void) {
	uint32_t sps[] = { 0x20000100, 0x20004004, 0x20003FFE, 0x20000000, 0 };
	for (unsigned i = 0; i < sizeof sps / sizeof sps[0]; i++) {
		img_opts_t o = img_defaults();
		o.sp = sps[i];
		expect_header(o, FAC_BOOT_STAY_BAD_VECTORS);
	}
	uint32_t entries[] = { 0x08004100, 0x08003FFF, 0x0801F801, 0x0801F800, 0x08000001, 0x0801FFFF };
	for (unsigned i = 0; i < sizeof entries / sizeof entries[0]; i++) {
		img_opts_t o = img_defaults();
		o.entry = entries[i];
		expect_header(o, FAC_BOOT_STAY_BAD_VECTORS);
	}
	// limits that are allowed
	img_opts_t o = img_defaults();
	o.sp = 0x20004000;
	o.entry = 0x08004001;
	sim_reset();
	app_install(&o, APP_SIZE, true, NULL);
	CHECK_EQ(check().reason, FAC_BOOT_STAY_NONE);
	o.sp = 0x20000104;
	o.entry = 0x0801F7FF;
	sim_reset();
	app_install(&o, APP_SIZE, true, NULL);
	CHECK_EQ(check().reason, FAC_BOOT_STAY_NONE);
}

static void test_debug(void) {
	img_opts_t o = img_defaults();
	o.flags = FAC_BOOT_APP_FLAG_DEBUG;
	app_install(&o, APP_SIZE, false, NULL);
	FAC_BL_app_status_t s = check();
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NONE);
	CHECK_EQ(s.state, FAC_BL_APP_STATE_DEBUG);
	CHECK(s.debug);

	// a Debug build is still checked for header and vectors
	sim_reset();
	o.sp = 0x20004004;
	app_install(&o, APP_SIZE, false, NULL);
	CHECK_EQ(check().reason, FAC_BOOT_STAY_BAD_VECTORS);
}

static void test_check_image_size(void) {
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, false, NULL);
	CHECK_EQ(FAC_BL_app_check_image(0), FAC_BOOT_STAY_BAD_HEADER);
	CHECK_EQ(FAC_BL_app_check_image(0xCF), FAC_BOOT_STAY_BAD_HEADER);
	CHECK_EQ(FAC_BL_app_check_image(0xD0), FAC_BOOT_STAY_NONE);
}

static void test_read_header(void) {
	FAC_boot_app_header_t h;
	CHECK(!FAC_BL_app_read_header(&h));
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, false, NULL);
	CHECK(FAC_BL_app_read_header(&h));
	CHECK_EQ(h.fw_version, 20100);
}

/* Shared block */
static void test_shared(void) {
	volatile FAC_boot_shared_t *sh = FAC_BL_port_shared();
	FAC_BL_shared_reset();
	CHECK_EQ(sh->magic, FAC_BOOT_MAGIC_SHARED);
	CHECK(FAC_BL_shared_is_valid());
	CHECK_EQ(sh->request, 0);
	CHECK_EQ(sh->boot_attempts, 0);
	CHECK_EQ(sh->stay_reason, 0);

	sh->boot_attempts = 2;	// changed without seal
	CHECK(!FAC_BL_shared_is_valid());
	FAC_BL_shared_seal();
	CHECK(FAC_BL_shared_is_valid());

	// check word over the four first words
	uint32_t *w = (uint32_t *) sh;
	CHECK_EQ(w[4], ~(w[0] ^ w[1] ^ w[2] ^ w[3]));

	sh->reset_flags = 0x1234;
	CHECK(!FAC_BL_shared_is_valid());
	FAC_BL_shared_seal();
	sh->request = FAC_BOOT_REQUEST_STAY;
	CHECK(!FAC_BL_shared_is_valid());
	FAC_BL_shared_seal();
	CHECK(FAC_BL_shared_is_valid());

	sim_shared_garbage();
	CHECK(!FAC_BL_shared_is_valid());
	// right magic, wrong check
	sh->magic = FAC_BOOT_MAGIC_SHARED;
	CHECK(!FAC_BL_shared_is_valid());
}

int main(void) {
	RUN(test_erased);
	RUN(test_valid);
	RUN(test_no_record);
	RUN(test_bad_record);
	RUN(test_crc_mismatch);
	RUN(test_record_fw_mismatch);
	RUN(test_bad_header);
	RUN(test_bad_vectors);
	RUN(test_debug);
	RUN(test_check_image_size);
	RUN(test_read_header);
	RUN(test_shared);
	return TEST_FINISH();
}
