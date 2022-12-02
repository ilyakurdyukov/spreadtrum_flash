#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>

#define ALIGN(n) __attribute__((aligned(n)))

#define ACT_AS_ROMCODE    0

#define HDLC_HEADER       0x7E
#define HDLC_ESCAPE       0x7D

void *memcpy(void *dst, const void *src, size_t count);
void *memmove(void *dst, const void *src, size_t count);
void *memset(void *dst, int ch, size_t count);

#if ACT_AS_ROMCODE
unsigned dl_crc16(const void *buf, unsigned len);
#else
unsigned dl_fastchk16(const uint16_t *src, unsigned len);
#endif

#define MEM2(addr) *(volatile uint16_t*)(addr)
#define MEM4(addr) *(volatile uint32_t*)(addr)
#define DELAY(n) { \
	unsigned _count = n; \
	do asm volatile(""); while (--_count); \
}

#define WRITE16_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 8; \
	((uint8_t*)(p))[1] = (uint8_t)(a); \
} while (0)

#define WRITE32_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 24; \
	((uint8_t*)(p))[1] = (a) >> 16; \
	((uint8_t*)(p))[2] = (a) >> 8; \
	((uint8_t*)(p))[3] = (uint8_t)(a); \
} while (0)

#define READ16_BE(p) ( \
	((uint8_t*)(p))[0] << 8 | \
	((uint8_t*)(p))[1])

#define READ32_BE(p) ( \
	((uint8_t*)(p))[0] << 24 | \
	((uint8_t*)(p))[1] << 16 | \
	((uint8_t*)(p))[2] << 8 | \
	((uint8_t*)(p))[3])

#ifndef __BYTE_ORDER__
#error
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
uint16_t swap_be16(uint16_t v);
uint32_t swap_be32(uint32_t v);
#else
static inline uint16_t swap_be16(uint16_t v) { return v; }
static inline uint32_t swap_be32(uint32_t v) { return v; }
#endif

#if CHIP == 0
extern int _chip;
#else
#define _chip CHIP
#endif

void dl_main(void);

#endif  // COMMON_H
