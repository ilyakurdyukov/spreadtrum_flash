static void sc6530_init_smc(void) {
	uint32_t base = 0x20000000, ps = 0x34000000;
	MEM4(base + 0x00) = 0x22220000;
	MEM4(base + 0x04) = 0;
	MEM4(base + 0x20) = 0;
	MEM4(base + 0x24) = 0x00924ff0;
	MEM4(base + 0x28) = 0x0151ffff;
	MEM4(base + 0x2c) = 0x00a0744f;
	DELAY(100)
	MEM4(base + 0x00) = 0x222211e0;
	MEM4(base + 0x04) = 0x8080;
	DELAY(10)
	MEM4(base + 0x24) |= 0x20000;
	MEM2(ps + 0x10323e) = 0;
	MEM2(ps + 0x20) = 0;
	DELAY(10)
	MEM4(base + 0x24) &= ~0x20000;
	DELAY(10)
	MEM4(base + 0x24) = 0x00ac1fff;
	MEM4(base + 0x28) = 0x015115ff;
	MEM4(base + 0x2c) = 0x00501015;
	MEM4(base + 0x00) = 0x222210e0;
	MEM4(base + 0x04) = 0x8080;
	DELAY(100)
}

static void sc6530_init_first(void) {
	uint32_t a;
	MEM4(0x8b0000a0) = -2;
	MEM4(0x8b0000a8) = -2;
	// CPU freq
	a = MEM4(0x8b000040);
	MEM4(0x8b000040) = (a & ~3) | 1;	// 208 MHz
	DELAY(100)
}

static void init_sc6530(void) {
	sc6530_init_first();
	sc6530_init_smc();
}

