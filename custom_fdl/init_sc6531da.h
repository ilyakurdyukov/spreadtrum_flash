#define get_freq_2() (312000000 >> 1)

static inline uint32_t sc6531da_get_psram(void) {
	// check remap
	uint32_t tmp = MEM4(0x205000e0) & 1;
	return tmp << 29 | tmp << 28 | 0x04000000;
}

static inline void sc6531da_init_smc_1(uint32_t base) {
	MEM4(base + 0x00) = 0x11110000;
	MEM4(base + 0x20) = 0;
	MEM4(base + 0x24) = 0;
	MEM4(base + 0x04) = 0x10;
	MEM4(base + 0x50) = 0x00904ff0;
	MEM4(base + 0x54) = 0x0151ffff;
	MEM4(base + 0x58) = 0x00a0744f;
	MEM4(base + 0x5c) = 0;
}

static void sc6531da_init_smc(void) {
	uint32_t base = 0x20000000, a, ps;
	int conf0 = 0, conf1 = 3, conf2 = 0, conf3 = 1;
	int conf4 = 7, conf5 = 2, conf6 = 2;
	uint32_t freq = get_freq_2();

	uint32_t freq2, div = 0, freq_div, ps_off1, ps_off2;
	uint32_t smc00 = 0, smc04 = 0, smc08 = 0, smc20 = 0, smc24 = 0;
	uint32_t smc50 = 0, smc54 = 0, smc58, smc5c = 0;

	freq2 = freq < 133000000 ? 3 : freq < 156000000 ? 4 : 5;
	ps_off1 = freq2 << 11;

	smc54 = (smc54 & (0xf << 20)) | (5 << 20);
	smc00 &= 0xffff800c;

	if (conf1 == 0) {
		smc54 |= 0xf00;
		smc54 = (smc54 & (0xf << 16)) | (1 << 16);
		smc54 &= ~(3 << 24); smc54 |= 1 << 24;
		smc54 &= ~(0xf << 12); smc54 |= 1 << 12;
		smc54 &= ~(0xf << 4); smc54 |= 1 << 4;

		smc58 = 0xa0744f;
		smc20 &= ~0x80;
		smc24 &= ~0x80;
		smc50 &= ~0x1000;
		ps_off1 |= 0x400;
		smc50 |= 0x4000;
	} else {
		smc00 |= 0x11e0;
		smc20 |= 0x80;
		smc24 |= 0x80;
		if (conf6 == 1) {
			smc54 &= ~0xf00;
			smc54 |= (freq2 + 2) << 9;
			smc54 &= ~0x3000000; smc54 |= 0x2000000;
			smc54 &= ~0xf000; smc54 |= 0x2000;
			smc54 &= ~0xf0; smc54 |= 0x20;

			smc58 = 0xa0202a;
			smc00 |= 0x10;
		} else if (conf6 == 2) {
			smc54 &= ~0xf00;
			smc54 |= (freq2 + 2) << 8;
			smc54 &= ~0x3000000; smc54 |= 0x1000000;
			smc54 &= ~0xf000; smc54 |= 0x1000;
			smc54 &= ~0xf0; smc54 |= 0x10;
			smc58 = 0x501015;

			smc00 &= ~0x20;
			smc54 &= ~0x500000;
			smc04 &= ~0xf; smc04 |= 7;
			smc20 &= ~0x80;
			smc24 &= ~0x80;
		} else {
			smc54 &= ~0xf00;
			smc54 |= (freq2 + 2) << 8;
			smc54 &= ~0x3000000; smc54 |= 0x1000000;
			smc54 &= ~0xf000; smc54 |= 0x1000;
			smc54 &= ~0xf0; smc54 |= 0x10;
			smc58 = 0x501015;
		}
		smc54 &= ~0xf0000; smc54 |= 0x20000;
		smc50 |= 0x1000;
	}

	if (conf0 == 1) {
		smc50 |= 0x8000000;
		smc08 |= 0x200;
		smc08 &= ~0xf000;
		smc08 |= 0x3580;
		smc00 |= 0x8000;

		smc54 &= ~0x3000000; smc54 |= 0x2000000;
		smc54 &= ~0xf00;
		smc54 |= (freq2 + 1) << 8;
		smc5c = 0xed000000;
		// unknown |= 0x80;
		div = 1;
	}

	freq_div = div ? freq / (7812500 / div) : 0;
	smc54 |= 0xf;

	smc00 |= 0x22220000;
	ps_off1 |= 0x80000;
	ps_off1 |= (conf1 == 0) << 15;
	ps_off1 |= conf2 << 14;
	ps_off1 |= conf0 << 9;
	ps_off1 |= conf3 << 3;
	ps_off1 |= conf4;
	ps_off1 |= 0x110;
	ps_off2 = 0x10;

	smc50 &= ~0x00700fff;
	smc50 |= conf5 << 20; // 3
	smc50 |= conf3 << 11 | conf4 << 8; // 1, 3
	smc50 |= conf3 << 7 | conf4 << 4; // 1, 3
	smc50 |= conf1 << 2 | conf1; // 2, 2
	smc50 |= 0x8c0000;
	smc04 |= 0x10;

	sc6531da_init_smc_1(base);

	ps = sc6531da_get_psram();

	MEM4(base + 0x50) |= 0x20000;
	MEM2(ps + (ps_off1 << 1)) = 0;
	DELAY(10);
	MEM2(ps + (ps_off2 << 1)) = 0;
	DELAY(10);
	MEM4(base + 0x50) &= ~0x20000;
	DELAY(10);

	a = freq_div | conf0 << 8;
	MEM4(base + 0x24) |= a;
	MEM4(base + 0x28) |= a;
	MEM4(base + 0x2c) |= a;
	MEM4(base + 0x30) |= a;
	MEM4(base + 0x34) |= a;
	MEM4(base + 0x38) |= a;

	MEM4(base + 0x08) = smc08;
	MEM4(base + 0x08) &= ~0x100;
	MEM4(base + 0x00) = smc00;
	MEM4(base + 0x20) |= smc20;
	MEM4(base + 0x24) |= smc24;
	MEM4(base + 0x04) = smc04;

	MEM4(base + 0x50) = smc50;
	MEM4(base + 0x54) = smc54;
	MEM4(base + 0x58) = smc58;
	MEM4(base + 0x5c) = smc5c;
	MEM4(base + 0x00) &= ~0x100;
	DELAY(100);
}
#undef get_freq_2

static void sc6531da_init_first(void) {
	uint32_t a;
	// CPU freq
	a = MEM4(0x8b00004c);
	MEM4(0x8b00004c) = a & ~4;
	MEM4(0x8b00004c) = a | 3;	// 312 MHz
	DELAY(100)
	MEM4(0x8b000044) = (MEM4(0x8b000044) & ~(3 << 19)) | 1 << 19;
	DELAY(100)
}

static void sc6531da_init_power(void) {
	MEM4(0x8b0000a0) = 1 << 24;
	MEM4(0x8b000060) = 1 << 19;
	DELAY(100)
	MEM4(0x8b000064) = 1 << 19;
	// init analog
	MEM4(0x82000000) &= ~(1 << 4);
	MEM4(0x82000004) = 0x55000;
}

static void init_sc6531da(void) {
	sc6531da_init_first();
	sc6531da_init_smc();
	sc6531da_init_power();
}

