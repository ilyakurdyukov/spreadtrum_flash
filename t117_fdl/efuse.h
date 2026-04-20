
#define EFUSE_MAX 64
#define EFUSE_BASE 0x40240000
#define EFUSE_CR(x) MEM4(EFUSE_BASE + (x))

static void efuse_init(void) {
	MEM4(0x402e0000) |= 0x2000;
	// reset
	MEM4(0x402e0008) |= 0x4000;
	DELAY(100)
	MEM4(0x402e0008) &= ~0x4000;
}

static void efuse_off(void) {
	MEM4(0x402e0000) &= ~0x2000;
}

static uint32_t efuse_read(unsigned id, int dbl_bit) {
	uint32_t ret;
	EFUSE_CR(0x48) = 0xffff;
	EFUSE_CR(0x50) |= 2;
	EFUSE_CR(0x40) = 1 | dbl_bit << 2;
	EFUSE_CR(0x54) |= 4;
	ret = EFUSE_CR(0x1000 + id * 4);
	EFUSE_CR(0x54) &= ~4;
	return ret;
}


