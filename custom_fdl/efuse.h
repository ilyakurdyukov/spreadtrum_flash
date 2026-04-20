#include "syscode.h"

#define EFUSE_MAX (_chip == 1 ? 16 : 8)
#define EFUSE_BASE 0x82002000
#define EFUSE_CR(x) MEM4(EFUSE_BASE + (x))

static void efuse_init(void) {
	uint32_t addr = 0x8b0010a8;
	uint32_t rst = 0x8b001068, add = 0x1000;
	if (_chip != 1) addr = 0x8b0000a0, rst = 0x8b000060, add = 4;

	// enable
	MEM4(addr) = 1 << 27;
	// reset
	MEM4(rst) = 0x400000;
	DELAY(100)
	MEM4(rst + add) = 0x400000;

	EFUSE_CR(0x10) |= 2 << 28;
	EFUSE_CR(0x10) |= 1 << 28;
	EFUSE_CR(0x10) |= 8 << 28;
	sys_wait_ms(2);
}

static void efuse_off(void) {
	EFUSE_CR(0x10) &= ~(1 << 28);
	EFUSE_CR(0x10) &= ~(8 << 28);
	EFUSE_CR(0x10) &= ~(2 << 28);
	{
		uint32_t addr = 0x8b0020a8;
		if (_chip != 1) addr = 0x8b0000a4;
		MEM4(addr) = 1 << 27;
	}
}

static int efuse_read(unsigned id, uint32_t *ret) {
	uint32_t t0;
	EFUSE_CR(8) = id | id << 16;
	EFUSE_CR(0xc) |= 2;
	t0 = sys_timer_ms();
	while (EFUSE_CR(0x14) & 2)
		if (sys_timer_ms() - t0 > 2) return 1;
	*ret = EFUSE_CR(0);
	return 0;
}

