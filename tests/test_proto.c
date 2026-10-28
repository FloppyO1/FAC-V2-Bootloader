#include "test.h"
#include "helpers.h"
#include "sim_port.h"
#include "Bootloader/fac_bl.h"
#include "Bootloader/fac_bl_app.h"
#include "Bootloader/fac_bl_config.h"
#include "Bootloader/fac_bl_proto.h"
#include <string.h>

#define SH	(FAC_BL_port_shared())

static uint8_t img[APP_SIZE];	// image being sent by the tests

static void p32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t) v;
	p[1] = (uint8_t) (v >> 8);
	p[2] = (uint8_t) (v >> 16);
	p[3] = (uint8_t) (v >> 24);
}

static uint8_t status(const reply_t *r) {
	return r->payload[0];
}

/* Request with a single expected reply: checks cmd, seq and status */
static void expect(uint8_t cmd, uint8_t seq, const void *payload, uint16_t length, uint8_t want_status) {
	reply_t r = { 0 };
	int n = rq(cmd, seq, payload, length, &r);
	CHECK_EQ(n, 1);
	CHECK_EQ(r.cmd, cmd | 0x80);
	CHECK_EQ(r.seq, seq);
	CHECK(r.len >= 1);
	CHECK_EQ(status(&r), want_status);
}

static void do_erase(uint32_t size, uint8_t want_status) {
	uint8_t p[4];
	p32(p, size);
	expect(FAC_BL_CMD_ERASE, 1, p, 4, want_status);
}

static void write_chunk(uint32_t offset, const uint8_t *data, uint32_t length, uint8_t want_status) {
	uint8_t p[4 + 1024];
	p32(p, offset);
	memcpy(p + 4, data, length);
	expect(FAC_BL_CMD_WRITE, 2, p, (uint16_t) (4 + length), want_status);
}

static void do_verify(uint32_t size, uint32_t crc, uint8_t want_status) {
	uint8_t p[8];
	p32(p, size);
	p32(p + 4, crc);
	expect(FAC_BL_CMD_VERIFY, 3, p, 8, want_status);
}

static void make_img(void) {
	img_opts_t o = img_defaults();
	img_build(img, APP_SIZE, &o);
}

/* INFO */
static void check_info_common(const reply_t *r) {
	CHECK_EQ(r->len, 45);
	CHECK_EQ(r->payload[0], FAC_BL_OK);
	CHECK_EQ(r->payload[1], 1);
	CHECK_EQ(r->payload[2], 2);
	CHECK_EQ(rd16(&r->payload[3]), 10000);
	CHECK_EQ(rd32(&r->payload[5]), 0x08004000);
	CHECK_EQ(rd32(&r->payload[9]), 0x1B800);
	CHECK_EQ(rd16(&r->payload[13]), 2048);
	CHECK_EQ(rd16(&r->payload[15]), 1024);
	CHECK(memcmp(&r->payload[33], sim.uid, 12) == 0);
}

static void test_info_no_app(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	SH->boot_attempts = 2;
	FAC_BL_shared_seal();
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_INFO, 7, NULL, 0, &r), 1);
	CHECK_EQ(r.cmd, 0x81);
	CHECK_EQ(r.seq, 7);
	check_info_common(&r);
	CHECK_EQ(r.payload[17], FAC_BOOT_STAY_NO_APP);
	CHECK_EQ(r.payload[18], 2);
	CHECK_EQ(r.payload[19], FAC_BL_APP_STATE_NONE);
	CHECK_EQ(rd32(&r.payload[21]), 0);
	CHECK_EQ(rd32(&r.payload[25]), 0);
	CHECK_EQ(rd16(&r.payload[29]), 0);
}

static void test_info_valid_app(void) {
	img_opts_t o = img_defaults();
	FAC_boot_record_t rec;
	app_install(&o, APP_SIZE, true, &rec);
	bl_enter(FAC_BOOT_STAY_REQUESTED);
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_INFO, 0x55, NULL, 0, &r), 1);
	CHECK_EQ(r.seq, 0x55);
	check_info_common(&r);
	CHECK_EQ(r.payload[17], FAC_BOOT_STAY_REQUESTED);
	CHECK_EQ(r.payload[19], FAC_BL_APP_STATE_VALID);
	CHECK_EQ(rd32(&r.payload[21]), APP_SIZE);
	CHECK_EQ(rd32(&r.payload[25]), rec.app_crc);
	CHECK_EQ(rd16(&r.payload[29]), 20100);
}

static void test_info_bad_length(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t p[1] = { 0 };
	expect(FAC_BL_CMD_INFO, 4, p, 1, FAC_BL_BAD_LENGTH);
}

/* Full update */
static void test_full_update(void) {
	make_img();
	bl_enter(FAC_BOOT_STAY_NO_APP);
	// an old record exists: ERASE must remove it first
	FAC_boot_record_t old = rec_make(img, APP_SIZE, 1);
	rec_write(&old);

	uint8_t p[4];
	p32(p, APP_SIZE);
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_ERASE, 1, p, 4, &r), 1);
	CHECK_EQ(r.cmd, 0x82);
	CHECK_EQ(status(&r), FAC_BL_OK);
	CHECK_EQ(r.len, 3);
	CHECK_EQ(rd16(&r.payload[1]), 2);
	CHECK_EQ(sim.erase_log_n, 3);
	CHECK_EQ(sim.erase_log[0], REC_ADDR);	// record page first
	CHECK_EQ(sim.erase_log[1], APP_BASE);
	CHECK_EQ(sim.erase_log[2], APP_BASE + 2048);
	FAC_boot_record_t tmp;
	CHECK(!FAC_BL_app_read_record(&tmp));
	CHECK(!sim.unlocked);

	for (uint32_t off = 0; off < APP_SIZE; off += 1024)
		write_chunk(off, img + off, 1024, FAC_BL_OK);
	CHECK(memcmp(FAC_BL_port_flash(APP_BASE), img, APP_SIZE) == 0);

	uint32_t crc = ref_crc32(0, img, APP_SIZE);
	p32(p, 0);
	uint8_t v[8];
	p32(v, APP_SIZE);
	p32(v + 4, crc);
	CHECK_EQ(rq(FAC_BL_CMD_VERIFY, 3, v, 8, &r), 1);
	CHECK_EQ(r.cmd, 0x84);
	CHECK_EQ(status(&r), FAC_BL_OK);
	CHECK_EQ(r.len, 5);
	CHECK_EQ(rd32(&r.payload[1]), crc);
	FAC_boot_record_t rec;
	CHECK(FAC_BL_app_read_record(&rec));
	CHECK_EQ(rec.app_size, APP_SIZE);
	CHECK_EQ(rec.app_crc, crc);
	CHECK_EQ(rec.fw_version, 20100);
	FAC_BL_app_status_t s;
	FAC_BL_app_check(&s);
	CHECK_EQ(s.reason, FAC_BOOT_STAY_NONE);

	// BOOT: reply first, reset after 50 ms
	SH->boot_attempts = 2;
	SH->request = FAC_BOOT_REQUEST_STAY;
	FAC_BL_shared_seal();
	CHECK_EQ(rq(FAC_BL_CMD_BOOT, 4, NULL, 0, &r), 1);
	CHECK_EQ(r.cmd, 0x85);
	CHECK_EQ(status(&r), FAC_BL_OK);
	CHECK_EQ(sim.reset_calls, 0);
	CHECK_EQ(SH->boot_attempts, 0);
	CHECK_EQ(SH->request, 0);
	CHECK(FAC_BL_shared_is_valid());
	bl_advance(49);
	CHECK_EQ(sim.reset_calls, 0);
	bl_advance(1);
	CHECK_EQ(sim.reset_calls, 1);
	bl_advance(500);
	CHECK_EQ(sim.reset_calls, 1);
}

static void test_write_repeat_and_conflict(void) {
	make_img();
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_erase(APP_SIZE, FAC_BL_OK);
	write_chunk(0, img, 1024, FAC_BL_OK);
	uint32_t programs = sim.program_count;
	CHECK(programs > 0);
	write_chunk(0, img, 1024, FAC_BL_OK);	// repeated: nothing more to program
	CHECK_EQ(sim.program_count, programs);

	uint8_t other[1024];
	for (int i = 0; i < 1024; i++)
		other[i] = (uint8_t) ~img[i];
	write_chunk(0, other, 1024, FAC_BL_NOT_ERASED);
	CHECK(memcmp(FAC_BL_port_flash(APP_BASE), img, 1024) == 0);
	CHECK_EQ(sim.program_count, programs);
	// partly written: nothing at all is programmed
	uint8_t mixed[16];
	memcpy(mixed, other, 16);
	write_chunk(1024 - 8, mixed, 16, FAC_BL_NOT_ERASED);
	CHECK(memcmp(FAC_BL_port_flash(APP_BASE), img, 1024) == 0);
	CHECK_EQ(sim.program_count, programs);
}

static void test_write_errors(void) {
	make_img();
	bl_enter(FAC_BOOT_STAY_NO_APP);
	write_chunk(0, img, 4, FAC_BL_BAD_STATE);	// before ERASE
	CHECK_EQ(sim.program_count, 0);

	do_erase(APP_SIZE, FAC_BL_OK);
	uint8_t p[4 + 1100];
	memset(p, 0x11, sizeof p);
	// misaligned offset / length
	p32(p, 2);
	expect(FAC_BL_CMD_WRITE, 2, p, 8, FAC_BL_MISALIGNED);
	p32(p, 0);
	expect(FAC_BL_CMD_WRITE, 2, p, 10, FAC_BL_MISALIGNED);
	// beyond the erased area
	write_chunk(4096, img, 4, FAC_BL_OUT_OF_RANGE);
	write_chunk(4092, img, 8, FAC_BL_OUT_OF_RANGE);
	write_chunk(0x20000, img, 4, FAC_BL_OUT_OF_RANGE);
	write_chunk(4092, img + 4092, 4, FAC_BL_OK);	// exactly the end
	// length
	p32(p, 0);
	expect(FAC_BL_CMD_WRITE, 2, p, 1032, FAC_BL_BAD_LENGTH);	// 1028 data bytes
	expect(FAC_BL_CMD_WRITE, 2, p, 4, FAC_BL_BAD_LENGTH);		// no data
	expect(FAC_BL_CMD_WRITE, 2, p, 7, FAC_BL_BAD_LENGTH);
	expect(FAC_BL_CMD_WRITE, 2, NULL, 0, FAC_BL_BAD_LENGTH);
	write_chunk(0, img, 1024, FAC_BL_OK);	// the maximum is fine
}

static void test_write_limited_to_pages_erased(void) {
	make_img();
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_erase(100, FAC_BL_OK);	// one page
	write_chunk(2040, img, 8, FAC_BL_OK);
	write_chunk(2048, img, 4, FAC_BL_OUT_OF_RANGE);
	// a new ERASE starts again
	do_erase(4096, FAC_BL_OK);
	write_chunk(2048, img, 4, FAC_BL_OK);
}

static void test_erase_errors(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_erase(0, FAC_BL_OUT_OF_RANGE);
	do_erase(0x1B801, FAC_BL_OUT_OF_RANGE);
	do_erase(0xFFFFFFFF, FAC_BL_OUT_OF_RANGE);
	CHECK_EQ(sim.erase_count, 0);
	uint8_t p[8] = { 0 };
	expect(FAC_BL_CMD_ERASE, 1, p, 0, FAC_BL_BAD_LENGTH);
	expect(FAC_BL_CMD_ERASE, 1, p, 3, FAC_BL_BAD_LENGTH);
	expect(FAC_BL_CMD_ERASE, 1, p, 8, FAC_BL_BAD_LENGTH);
	reply_t r;
	uint8_t s[4];
	p32(s, 0x1B800);
	CHECK_EQ(rq(FAC_BL_CMD_ERASE, 1, s, 4, &r), 1);
	CHECK_EQ(status(&r), FAC_BL_OK);
	CHECK_EQ(rd16(&r.payload[1]), 55);
	CHECK_EQ(sim.erase_count, 56);
}

static void test_erase_failure(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	sim.fail_erase_page = (APP_BASE + 2048 - SIM_FLASH_BASE) / SIM_PAGE;
	do_erase(APP_SIZE, FAC_BL_ERASE_FAILED);
	sim.fail_erase_page = (REC_ADDR - SIM_FLASH_BASE) / SIM_PAGE;
	do_erase(APP_SIZE, FAC_BL_ERASE_FAILED);
	CHECK(!sim.unlocked);
}

static void test_write_failure(void) {
	make_img();
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_erase(APP_SIZE, FAC_BL_OK);
	sim.fail_program_addr = APP_BASE + 8;
	write_chunk(0, img, 64, FAC_BL_WRITE_FAILED);
	CHECK(!sim.unlocked);
	sim.fail_program_addr = SIM_NONE;
	sim.corrupt_program_addr = APP_BASE + 1024 + 4;	// wrong value written: read back differs
	write_chunk(1024, img + 1024, 64, FAC_BL_WRITE_FAILED);
}

/* VERIFY */
static void test_verify_errors(void) {
	img_opts_t o = img_defaults();
	uint8_t *image = app_install(&o, APP_SIZE, false, NULL);
	uint32_t crc = ref_crc32(0, image, APP_SIZE);
	bl_enter(FAC_BOOT_STAY_NO_APP);

	reply_t r;
	uint8_t v[8];
	p32(v, APP_SIZE);
	p32(v + 4, crc ^ 1);
	CHECK_EQ(rq(FAC_BL_CMD_VERIFY, 3, v, 8, &r), 1);
	CHECK_EQ(status(&r), FAC_BL_CRC_MISMATCH);
	CHECK_EQ(r.len, 5);
	CHECK_EQ(rd32(&r.payload[1]), crc);
	FAC_boot_record_t rec;
	CHECK(!FAC_BL_app_read_record(&rec));	// no record written
	CHECK_EQ(sim.erase_count, 0);

	do_verify(4098, crc, FAC_BL_MISALIGNED);
	do_verify(0, 0, FAC_BL_OUT_OF_RANGE);
	do_verify(0x1B804, 0, FAC_BL_OUT_OF_RANGE);
	uint8_t p[9] = { 0 };
	expect(FAC_BL_CMD_VERIFY, 3, p, 9, FAC_BL_BAD_LENGTH);
	expect(FAC_BL_CMD_VERIFY, 3, p, 4, FAC_BL_BAD_LENGTH);
	CHECK(!FAC_BL_app_read_record(&rec));

	// good one, without any ERASE in the session
	do_verify(APP_SIZE, crc, FAC_BL_OK);
	CHECK(FAC_BL_app_read_record(&rec));
}

static void test_verify_bad_header(void) {
	img_opts_t o = img_defaults();
	o.magic = 0;
	uint8_t *image = app_install(&o, APP_SIZE, false, NULL);
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_verify(APP_SIZE, ref_crc32(0, image, APP_SIZE), FAC_BL_BAD_HEADER);
	FAC_boot_record_t rec;
	CHECK(!FAC_BL_app_read_record(&rec));
	// image smaller than vectors + header
	sim_reset();
	o = img_defaults();
	image = app_install(&o, APP_SIZE, false, NULL);
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_verify(0xC8, ref_crc32(0, image, 0xC8), FAC_BL_BAD_HEADER);
}

static void test_verify_bad_vectors(void) {
	img_opts_t o = img_defaults();
	o.entry = 0x08004100;
	uint8_t *image = app_install(&o, APP_SIZE, false, NULL);
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_verify(APP_SIZE, ref_crc32(0, image, APP_SIZE), FAC_BL_BAD_VECTORS);
	FAC_boot_record_t rec;
	CHECK(!FAC_BL_app_read_record(&rec));
}

/* BOOT and unknown commands */
static void test_boot_no_app(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	expect(FAC_BL_CMD_BOOT, 1, NULL, 0, FAC_BL_NO_VALID_APP);
	bl_advance(200);
	CHECK_EQ(sim.reset_calls, 0);
	// invalid app (no record) as well
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, false, NULL);
	expect(FAC_BL_CMD_BOOT, 1, NULL, 0, FAC_BL_NO_VALID_APP);
	bl_advance(200);
	CHECK_EQ(sim.reset_calls, 0);
	uint8_t p[1] = { 0 };
	expect(FAC_BL_CMD_BOOT, 1, p, 1, FAC_BL_BAD_LENGTH);
}

static void test_unknown_command(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	expect(0x09, 9, NULL, 0, FAC_BL_UNKNOWN_COMMAND);
	reply_t r;
	CHECK_EQ(rq(0x09, 9, NULL, 0, &r), 1);
	CHECK_EQ(r.cmd, 0x89);
	expect(0x00, 1, NULL, 0, FAC_BL_UNKNOWN_COMMAND);
}

/* Framing */
static void test_bad_crc(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t f[16];
	uint32_t n = frame_build(f, FAC_BL_CMD_INFO, 0x42, NULL, 0);
	f[n - 1] ^= 0x80;
	sim_tx_clear();
	bl_feed(f, n);
	bl_run();
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0xFF);
	CHECK_EQ(r[0].seq, 0x42);
	CHECK_EQ(r[0].len, 1);
	CHECK_EQ(r[0].payload[0], FAC_BL_BAD_CRC);
	// and it recovers
	expect(FAC_BL_CMD_INFO, 0x43, NULL, 0, FAC_BL_OK);
}

static void test_len_too_big(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t f[200];
	memset(f, 0x33, sizeof f);
	f[0] = FAC_BL_SOF;
	f[1] = FAC_BL_CMD_WRITE;
	f[2] = 0x21;
	f[3] = (uint8_t) 1041;
	f[4] = (uint8_t) (1041 >> 8);
	sim_tx_clear();
	bl_feed(f, sizeof f);
	bl_run();
	CHECK_EQ(sim.tx_len, 0);
	bl_advance(19);
	CHECK_EQ(sim.tx_len, 0);
	bl_advance(1);
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0xFF);
	CHECK_EQ(r[0].seq, 0x21);
	CHECK_EQ(r[0].payload[0], FAC_BL_BAD_LENGTH);
	bl_advance(100);
	CHECK_EQ(replies_parse(r, 4), 1);	// only the one NAK
	expect(FAC_BL_CMD_INFO, 5, NULL, 0, FAC_BL_OK);
}

static void test_len_max_accepted(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t p[1040];
	memset(p, 0, sizeof p);
	expect(0x09, 3, p, 1040, FAC_BL_UNKNOWN_COMMAND);	// 1040 is a valid length
}

static void test_stray_bytes(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t junk[4] = { 3, 0, 0, 0 };
	sim_tx_clear();
	bl_feed(junk, 4);
	bl_run();
	CHECK_EQ(sim.tx_len, 0);
	bl_advance(19);
	CHECK_EQ(sim.tx_len, 0);
	bl_advance(1);
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0xFF);
	CHECK_EQ(r[0].seq, 0);
	CHECK_EQ(r[0].len, 1);
	CHECK_EQ(r[0].payload[0], FAC_BL_NOT_A_FRAME);
	bl_advance(1000);
	CHECK_EQ(replies_parse(r, 4), 1);	// exactly one
}

static void test_split_frames(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t f[16];
	uint32_t n = frame_build(f, FAC_BL_CMD_INFO, 0x31, NULL, 0);
	reply_t r[4];

	sim_tx_clear();	// one byte per USB packet and loop
	for (uint32_t i = 0; i < n; i++) {
		CHECK_EQ(sim.tx_len, 0);
		FAC_BL_usb_received(&f[i], 1);
		bl_run();
	}
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0x81);
	CHECK_EQ(r[0].seq, 0x31);

	sim_tx_clear();	// chunks of 4
	for (uint32_t i = 0; i < n; i += 4) {
		FAC_BL_usb_received(&f[i], n - i < 4 ? n - i : 4);
		bl_run();
	}
	CHECK_EQ(replies_parse(r, 4), 1);

	// a long frame split like the USB does (64 byte packets)
	make_img();
	do_erase(APP_SIZE, FAC_BL_OK);
	write_chunk(0, img, 1024, FAC_BL_OK);
}

static void test_two_frames_one_packet(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t pkt[64];
	uint32_t n = frame_build(pkt, FAC_BL_CMD_INFO, 1, NULL, 0);
	n += frame_build(pkt + n, 0x09, 2, NULL, 0);
	sim_tx_clear();
	FAC_BL_usb_received(pkt, n);
	bl_run();
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 2);
	CHECK_EQ(r[0].cmd, 0x81);
	CHECK_EQ(r[0].seq, 1);
	CHECK_EQ(r[1].cmd, 0x89);
	CHECK_EQ(r[1].seq, 2);
}

static void test_partial_frame_timeout(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t f[16];
	uint32_t n = frame_build(f, FAC_BL_CMD_INFO, 0x31, NULL, 0);
	sim_tx_clear();
	bl_feed(f, 5);
	bl_run();
	bl_advance(100);
	CHECK_EQ(sim.tx_len, 0);
	bl_advance(1);
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0xFF);
	CHECK_EQ(r[0].seq, 0);
	CHECK_EQ(r[0].payload[0], FAC_BL_NOT_A_FRAME);
	bl_advance(500);
	CHECK_EQ(replies_parse(r, 4), 1);
	expect(FAC_BL_CMD_INFO, 0x32, NULL, 0, FAC_BL_OK);
	(void) n;
}

static void test_partial_body_timeout(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t f[32];
	uint8_t p[8] = { 0 };
	uint32_t n = frame_build(f, 0x09, 0x31, p, 8);
	sim_tx_clear();
	bl_feed(f, n - 2);
	bl_run();
	bl_advance(101);
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].payload[0], FAC_BL_NOT_A_FRAME);
}

/* Direct use of the protocol layer */
static void test_direct_proto(void) {
	FAC_BL_proto_init(false, sim.now);
	uint8_t f[16];
	uint32_t n = frame_build(f, FAC_BL_CMD_INFO, 0x77, NULL, 0);
	sim_tx_clear();
	for (uint32_t i = 0; i < n; i++)
		FAC_BL_proto_byte(f[i], sim.now);
	reply_t r[4];
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].cmd, 0x81);
	CHECK_EQ(r[0].seq, 0x77);
	check_info_common(&r[0]);

	// stray byte and timeouts through poll
	sim_tx_clear();
	FAC_BL_proto_byte(0x00, sim.now);
	FAC_BL_proto_poll(sim.now + 19);
	CHECK_EQ(sim.tx_len, 0);
	FAC_BL_proto_poll(sim.now + 20);
	CHECK_EQ(replies_parse(r, 4), 1);
	CHECK_EQ(r[0].payload[0], FAC_BL_NOT_A_FRAME);
}

static void test_replies_are_frames(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_INFO, 1, NULL, 0, &r), 1);
	CHECK_EQ(sim.tx[0], FAC_BL_SOF);
	CHECK_EQ(sim.tx_len, 9 + 45);
	CHECK_EQ(rd16(&sim.tx[3]), 45);
	CHECK_EQ(rd32(&sim.tx[sim.tx_len - 4]), ref_crc32(0, &sim.tx[1], 4 + 45));
}

/* Idle return */
static void install_valid(void) {
	img_opts_t o = img_defaults();
	app_install(&o, APP_SIZE, true, NULL);
}

static void test_idle_return(void) {
	install_valid();
	bl_enter(FAC_BOOT_STAY_REQUESTED);
	bl_advance(59999);
	CHECK_EQ(sim.reset_calls, 0);
	bl_advance(1);
	CHECK_EQ(sim.reset_calls, 1);
}

static void test_idle_restarted_by_frame(void) {
	install_valid();
	bl_enter(FAC_BOOT_STAY_REQUESTED);
	bl_advance(59000);
	CHECK_EQ(sim.reset_calls, 0);
	expect(FAC_BL_CMD_INFO, 1, NULL, 0, FAC_BL_OK);
	bl_advance(59999);
	CHECK_EQ(sim.reset_calls, 0);
	bl_advance(1);
	CHECK_EQ(sim.reset_calls, 1);
}

static void test_idle_not_after_erase(void) {
	install_valid();
	bl_enter(FAC_BOOT_STAY_REQUESTED);
	do_erase(APP_SIZE, FAC_BL_OK);
	bl_advance(60000);
	bl_advance(120000);
	bl_advance(600000);
	CHECK_EQ(sim.reset_calls, 0);
}

static void test_idle_not_for_no_app(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	bl_advance(120000);
	bl_advance(600000);
	CHECK_EQ(sim.reset_calls, 0);
	// requested, but the application is not valid
	bl_enter(FAC_BOOT_STAY_REQUESTED);
	bl_advance(120000);
	CHECK_EQ(sim.reset_calls, 0);
	// valid application but the stay was for another reason
	install_valid();
	bl_enter(FAC_BOOT_STAY_BOOT_FAILED);
	bl_advance(120000);
	CHECK_EQ(sim.reset_calls, 0);
}

/* USB ring */
static void test_ring_pause_and_rearm(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	uint8_t pkt[64] = { 0 };
	int calls = 0;
	bool accepted = true;
	while (accepted && calls < 100) {
		accepted = FAC_BL_usb_received(pkt, 64);
		calls++;
	}
	CHECK(!accepted);
	CHECK(calls >= 31 && calls <= 33);
	CHECK_EQ(sim.rearm_calls, 0);
	bl_run();	// drains the ring
	CHECK_EQ(sim.rearm_calls, 1);
	bl_run();
	CHECK_EQ(sim.rearm_calls, 1);	// only once
	CHECK(FAC_BL_usb_received(pkt, 64));	// room again
}

static void test_no_rearm_when_not_paused(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	expect(FAC_BL_CMD_INFO, 1, NULL, 0, FAC_BL_OK);
	CHECK_EQ(sim.rearm_calls, 0);
}

/* TX */
static void test_tx_busy_forever(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	sim.busy_forever = true;
	uint32_t start = sim.now;
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_INFO, 1, NULL, 0, &r), 0);
	CHECK(sim.now - start >= 100);
	CHECK(sim.now - start < 200);
	CHECK(sim.wd_kicks > 0);
}

static void test_tx_busy_briefly(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	sim.busy_calls = 5;
	reply_t r;
	CHECK_EQ(rq(FAC_BL_CMD_INFO, 1, NULL, 0, &r), 1);
	CHECK_EQ(r.cmd, 0x81);
}

/* LED */
static void test_led_idle(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	struct { uint32_t t; bool on; } cases[] = {
		{ 30, true }, { 100, false }, { 200, true }, { 500, false },
		{ 59, true }, { 60, false }, { 179, false }, { 180, true }, { 239, true }, { 240, false },
		{ 999, false }, { 0, true },
	};
	for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; i++) {
		sim.now = 10000 + cases[i].t + 1000U * (i + 1);
		bl_run();
		if (sim.led != cases[i].on)
			printf("  t=%u now=%u led=%d toggles=%u\n", (unsigned) cases[i].t, (unsigned) sim.now, sim.led,
					(unsigned) sim.led_toggles);
		CHECK(sim.led == cases[i].on);
	}
}

static void test_led_writing(void) {
	bl_enter(FAC_BOOT_STAY_NO_APP);
	do_erase(APP_SIZE, FAC_BL_OK);
	// each valid frame toggles it
	bool prev = sim.led;
	for (int i = 0; i < 6; i++) {
		expect(FAC_BL_CMD_INFO, (uint8_t) i, NULL, 0, FAC_BL_OK);
		CHECK(sim.led != prev);
		prev = sim.led;
	}
	// the idle blink pattern stays off while the packets rule, then returns
	uint32_t toggles = sim.led_toggles;
	bl_run();
	CHECK_EQ(sim.led_toggles, toggles);
	// a frame with a bad CRC does not toggle
	uint8_t f[16];
	uint32_t n = frame_build(f, FAC_BL_CMD_INFO, 1, NULL, 0);
	f[n - 1] ^= 1;
	prev = sim.led;
	bl_feed(f, n);
	bl_run();
	CHECK(sim.led == prev);
}

int main(void) {
	RUN(test_info_no_app);
	RUN(test_info_valid_app);
	RUN(test_info_bad_length);
	RUN(test_full_update);
	RUN(test_write_repeat_and_conflict);
	RUN(test_write_errors);
	RUN(test_write_limited_to_pages_erased);
	RUN(test_erase_errors);
	RUN(test_erase_failure);
	RUN(test_write_failure);
	RUN(test_verify_errors);
	RUN(test_verify_bad_header);
	RUN(test_verify_bad_vectors);
	RUN(test_boot_no_app);
	RUN(test_unknown_command);
	RUN(test_bad_crc);
	RUN(test_len_too_big);
	RUN(test_len_max_accepted);
	RUN(test_stray_bytes);
	RUN(test_split_frames);
	RUN(test_two_frames_one_packet);
	RUN(test_partial_frame_timeout);
	RUN(test_partial_body_timeout);
	RUN(test_direct_proto);
	RUN(test_replies_are_frames);
	RUN(test_idle_return);
	RUN(test_idle_restarted_by_frame);
	RUN(test_idle_not_after_erase);
	RUN(test_idle_not_for_no_app);
	RUN(test_ring_pause_and_rearm);
	RUN(test_no_rearm_when_not_paused);
	RUN(test_tx_busy_forever);
	RUN(test_tx_busy_briefly);
	RUN(test_led_idle);
	RUN(test_led_writing);
	return TEST_FINISH();
}
