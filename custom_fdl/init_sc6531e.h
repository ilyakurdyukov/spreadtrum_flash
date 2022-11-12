static inline uint32_t get_psram(void) {
	// check remap
	return (MEM4(0x205000e0) & 1) << 28 | 0x04000000;
}

static int smc_get_id(uint32_t base, uint32_t ps) {
	uint32_t a;

	MEM4(base + 0x88) = 3;
	MEM4(base + 0x8c) &= ~1;
	MEM4(base + 0x8c) |= 1;
	DELAY(1000)
	MEM4(base + 0x84) &= ~1;
	a = *(volatile uint8_t*)(ps + 1) & 0x1f;
	DELAY(100)
	MEM4(base + 0x84) |= 1;
	return a;
}

static void smc_save_id(uint32_t base, uint32_t ps, uint32_t id) {
	uint32_t a, b; 

	MEM4(0x20500168) = 0;
	MEM4(base + 0x84) &= ~1;
	a = *(volatile uint8_t*)(ps + 1) & 0x20;
	DELAY(100)
	b = *(volatile uint8_t*)(ps + 2) & 7;
	DELAY(100)
	MEM4(base + 0x84) |= 1;
	MEM4(0x20500168) = id << 8 | a >> 2 | b;
}

typedef struct {
	uint32_t x04_18, x20_24, x45_48, x58_68;
	uint8_t x28, x2c, x30, x34, x38, x3c, x40;
	uint8_t x1c, x44, x4c, x50_54, x6c;
} smc_config_t;

#define SMC_CONFIG(x00, x04, x08, x0c, \
		x10, x14, x18, x1c, x20, x24, x28, x2c, x30, x34, x38, x3c, \
		x40, x44, x45, x46, x47, x48, x4c, \
		x50, x54, x58, x5c, x60, x64, x68, x6c) { \
	x04 << 28 | x08 << 24 | x0c << 20 | x10 << 16 | x14 << 11 | x18 << 6, \
	x20 << 12 | x24, \
	x45 << 20 | x47 << 11 | x47 << 7 | x46 << 8 | x46 << 4 | x48 << 2 | x48, \
	x58 << 16 | x5c << 14 | x60 << 8 | x64 << 6 | x68, \
	x28, x2c, x30, x34, x38, x3c, x40, \
	x1c, x44, x4c, x50 << 2 | x54, x6c \
}

// SRAM memory controller
static void init_smc() {
	uint32_t id, a;

	const smc_config_t *conf;
	static const smc_config_t conf_06 = SMC_CONFIG(
		/* 0x00 */ 6, 5, 5, 5, /* 0x10 */ 5, 5, 2, 1,
		/* 0x20 */ 0, 0, 0, 0, /* 0x30 */ 0x50, 0x50, 0x50, 0,
		/* 0x40 */ 0, 2,4,7,1, 3, 0, /* 0x50 */ 5, 2, 6, 1,
		/* 0x60 */ 3, 1, 0x10, 0x34);
	static const smc_config_t conf_0d = SMC_CONFIG(
		/* 0x00 */ 13, 3, 3, 3, /* 0x10 */ 5, 4, 3, 1,
		/* 0x20 */ 0, 0, 0, 0, /* 0x30 */ 0x50, 0x50, 0x50, 0,
		/* 0x40 */ 0, 2,4,7,1, 3, 0, /* 0x50 */ 7, 1, 5, 1,
		/* 0x60 */ 3, 1, 0xA, 0x12);

	uint32_t base = 0x20000000;
	uint32_t ps;

	MEM4(base + 0x00) = 0x3333a0c0;
	MEM4(base + 0x04) = 7;
	MEM4(base + 0x20) = 15;
	MEM4(base + 0x24) = 15;
	MEM4(base + 0x28) = 15;
	MEM4(base + 0x2c) = 10;
	MEM4(base + 0x30) = 10;
	MEM4(base + 0x34) = 10;
	MEM4(base + 0x38) = 10;
	MEM4(base + 0x50) = 0x0acc177f;
	MEM4(base + 0x5c) = 0x1d000000;

	MEM4(base + 0x94) = 0x5434a;
	MEM4(base + 0x00) |= 0x100;
	MEM4(base + 0x00) &= ~0x100;
	DELAY(100)

	ps = get_psram();
	id = smc_get_id(base, ps);
	if (id == 6) {
		MEM4(base + 0xa0) = 0x5a5a;
		MEM4(base + 0x80) |= 1;
		conf = &conf_06;
	} else if (id == 13) {
		MEM4(base + 0xa0) = 0x5a5a;
		MEM4(base + 0x80) &= ~1;
		conf = &conf_0d;
	}	else for (;;);
	smc_save_id(base, ps, id);

	a = MEM4(base + 0x00) & ~0x333378c0;
	MEM4(base + 0x00) = a | conf->x04_18;
	MEM4(base + 0x20) &= ~0x7f;
	MEM4(base + 0x24) &= ~0x7f;
	MEM4(base + 0x28) &= ~0x7f;
	MEM4(base + 0x2c) &= ~0x7f;
	MEM4(base + 0x30) &= ~0x7f;
	MEM4(base + 0x34) &= ~0x7f;
	MEM4(base + 0x38) &= ~0x7f;

	MEM4(base + 0x20) |= conf->x28;
	MEM4(base + 0x24) |= conf->x2c;
	MEM4(base + 0x28) |= conf->x30;
	MEM4(base + 0x2c) |= conf->x34;
	MEM4(base + 0x30) |= conf->x38;
	MEM4(base + 0x34) |= conf->x3c;
	MEM4(base + 0x38) |= conf->x40;

	if (conf->x1c) {
		a = MEM4(base + 0x08) & ~0xf4ff;
		MEM4(base + 0x08) = a | conf->x20_24;
		MEM4(base + 0x28) |= 0x100;
		MEM4(base + 0x2c) |= 0x100;
		MEM4(base + 0x30) |= 0x100;
		MEM4(base + 0x08) |= 0x480;
	}

	if (conf->x44 == 2)
		MEM4(base + 0x50) |= 0x2800000;
	a = MEM4(base + 0x50) & ~0x700fff;
	MEM4(base + 0x50) = a | conf->x45_48;
	MEM4(base + 0x50) |= 0x80c0000;
	a = MEM4(base + 0x54) & ~0x3000000;
	MEM4(base + 0x54) = a | conf->x4c << 24;
	a = MEM4(base + 0x5c) & ~0x1f000000;
	MEM4(base + 0x5c) = a | conf->x50_54 << 24;
	a = MEM4(base + 0x94) & ~0x1fffff;
	MEM4(base + 0x94) = a | conf->x58_68;

	MEM4(base + 0x84) &= ~1;
	a = get_psram();
	*(volatile uint8_t*)a = conf->x6c;
	DELAY(100)
	MEM4(base + 0x84) |= 1;
}

static void init_first() {
	uint32_t a;
	a = MEM4(0x8b00004c);
	MEM4(0x8b00004c) = a & ~4;
	MEM4(0x8b00004c) = a | 3;
	DELAY(100)
	MEM4(0x8d20002c) = (MEM4(0x8d20002c) & ~7) | 2;
	MEM4(0x8d200030) = (MEM4(0x8d200030) & ~0x300) | 0x100;
	DELAY(100)
}

static void init_power() {
	MEM4(0x8b0010a8) = 1 << 24;
	MEM4(0x8b001068) = 1 << 19;
	DELAY(100)
	MEM4(0x8b002068) = 1 << 19;
	MEM4(0x82000010) &= ~(1 << 30);
	MEM4(0x82000000) = 0;
}

static void init_sc6531e(void) {
	init_first();
	init_smc();
	init_power();
}

