/**
 * @file	fac_bl_crc.c
 * @brief	CRC-32 (zlib) with the CRC peripheral for whole words, in software for the odd bytes.
 */
#include "Bootloader/fac_bl_crc.h"
#include "Bootloader/fac_bl_port.h"

#define CRC32_POLY_REFLECTED	0xEDB88320UL

/* Bit reversal of a word: the peripheral works MSB first, zlib LSB first */
static uint32_t reflect32(uint32_t value) {
	uint32_t result = 0;
	for (uint8_t i = 0; i < 32; i++) {
		result = (result << 1) | (value & 1U);
		value >>= 1;
	}
	return result;
}

/* Bitwise update of the running value (no table, it handles at most 6 bytes per call) */
static uint32_t crc32_bytes(uint32_t state, const uint8_t *data, uint32_t length) {
	while (length--) {
		state ^= *data++;
		for (uint8_t bit = 0; bit < 8; bit++)
			state = (state >> 1) ^ (CRC32_POLY_REFLECTED & (0U - (state & 1U)));
	}
	return state;
}

uint32_t FAC_BL_crc32(uint32_t crc, const void *data, uint32_t length) {
	const uint8_t *bytes = (const uint8_t *) data;
	uint32_t state = ~crc;	// running value, as in zlib

	// leading bytes up to a word boundary (Cortex-M0 has no unaligned loads)
	uint32_t head = (4U - ((uintptr_t) bytes & 3U)) & 3U;
	if (head > length)
		head = length;
	state = crc32_bytes(state, bytes, head);
	bytes += head;
	length -= head;

	// whole words in the peripheral, starting from the running value
	uint32_t words = length / 4U;
	if (words) {
		state = FAC_BL_port_crc_words(reflect32(state), (const uint32_t *) bytes, words);
		bytes += words * 4U;
		length -= words * 4U;
	}

	return ~crc32_bytes(state, bytes, length);
}
