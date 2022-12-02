static void sc6531da_init_first(void) {
	uint32_t a;
	// CPU freq
	a = MEM4(0x8b00004c);
	MEM4(0x8b00004c) = a & ~4;
	MEM4(0x8b00004c) = a | 3;
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
	sc6531da_init_power();
}

