/**
 * @file	fac_bl_crc.h
 * @brief	CRC-32/ISO-HDLC (zlib): poly 0x04C11DB7 reflected, init and final XOR 0xFFFFFFFF.
 */
#ifndef FAC_BL_CRC_H
#define FAC_BL_CRC_H

#include <stdint.h>

/* zlib style: crc32(0, data, n) for a new CRC, or pass a previous result to continue it */
uint32_t FAC_BL_crc32(uint32_t crc, const void *data, uint32_t length);

#endif /* FAC_BL_CRC_H */
