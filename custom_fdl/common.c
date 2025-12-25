#include "common.h"

#if ACT_AS_ROMCODE
unsigned dl_crc16(const void *buf, unsigned len) {
	uint8_t *s = (uint8_t*)buf; int i;
	unsigned crc = 0;
	while (len--) {
		crc ^= *s++ << 8;
		for (i = 0; i < 8; i++)
			crc = crc << 1 ^ ((0 - (crc >> 15)) & 0x11021);
	}
	return crc;
}
#else
unsigned dl_fastchk16(const uint16_t *src, unsigned len) {
	uint32_t sum = 0;

	while (len > 3) {
		sum += *src++;
		sum += *src++;
		len -= 4;
	}
	if (len & 2) sum += *src++;
	if (len & 1) sum += *(const uint8_t*)src;

	sum = (sum >> 16) + (sum & 0xffff);
	sum += sum >> 16;
	return ~sum & 0xffff;
}
#endif

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
uint16_t swap_be16(uint16_t v) {
	return v >> 8 | v << 8;
}

uint32_t swap_be32(uint32_t v) {
	uint32_t t = v >> 24 | v << 24, m = 0xff00;
	return t | (v >> 8 & m) | (v & m) << 8;
}
#endif

// GCC with -flto removes memcpy without this attribute
__attribute__((used))
void *memcpy(void *dst, const void *src, size_t len) {
	uint8_t *d = (uint8_t*)dst;
	const uint8_t *s = (const uint8_t*)src;

	/* copy aligned data faster */
	if (!(((uintptr_t)s | (uintptr_t)d) & 3)) {
		unsigned len4 = len >> 2;		
		uint32_t *d4 = (uint32_t*)d;
		const uint32_t *s4 = (const uint32_t*)s;
		while (len4--) *d4++ = *s4++;
		len &= 3;
		d = (uint8_t*)d4;
		s = (const uint8_t*)s4;
	}
	while (len--) *d++ = *s++;
	return dst;
}

__attribute__((used))
void *memset(void *dst, int c, size_t len) {
	uint8_t *d = (uint8_t*)dst;
	while (len--) *d++ = c;
	return dst;
}

