#include "test.h"
#include "helpers.h"
#include "sim_port.h"
#include "Bootloader/fac_bl_crc.h"

static uint32_t storage[(112640U + 16U) / 4U];

static void fill(uint8_t *p, uint32_t n) {
	uint32_t x = 12345;
	for (uint32_t i = 0; i < n; i++) {
		x = x * 1664525U + 1013904223U;
		p[i] = (uint8_t) (x >> 24);
	}
}

static void test_vectors(void) {
	CHECK_EQ(FAC_BL_crc32(0, "123456789", 9), 0xCBF43926U);
	CHECK_EQ(FAC_BL_crc32(0, "", 0), 0);
	CHECK_EQ(ref_crc32(0, "123456789", 9), 0xCBF43926U);
}

static void test_lengths_alignments(void) {
	uint8_t *base = (uint8_t *) storage;
	fill(base, 128);
	for (uint32_t off = 0; off < 4; off++)
		for (uint32_t len = 0; len <= 64; len++) {
			uint32_t got = FAC_BL_crc32(0, base + off, len);
			uint32_t want = ref_crc32(0, base + off, len);
			if (got != want)
				printf("  off=%u len=%u\n", (unsigned) off, (unsigned) len);
			CHECK_EQ(got, want);
		}
}

static void test_continuation(void) {
	uint8_t *base = (uint8_t *) storage;
	fill(base, 64);
	uint32_t total = 45;
	uint32_t whole = ref_crc32(0, base, total);
	for (uint32_t split = 0; split <= total; split++) {
		uint32_t part = FAC_BL_crc32(0, base, split);
		CHECK_EQ(FAC_BL_crc32(part, base + split, total - split), whole);
	}
	// from an odd start address too
	uint32_t whole1 = ref_crc32(0, base + 1, total);
	for (uint32_t split = 0; split <= total; split++) {
		uint32_t part = FAC_BL_crc32(0, base + 1, split);
		CHECK_EQ(FAC_BL_crc32(part, base + 1 + split, total - split), whole1);
	}
}

static void test_large(void) {
	uint8_t *base = (uint8_t *) storage;
	fill(base, 0x1B800);
	CHECK_EQ(FAC_BL_crc32(0, base, 0x1B800), ref_crc32(0, base, 0x1B800));
	CHECK(sim.crc_calls > 0);	// the peripheral did the words
}

int main(void) {
	RUN(test_vectors);
	RUN(test_lengths_alignments);
	RUN(test_continuation);
	RUN(test_large);
	return TEST_FINISH();
}
