#include "common.h"

// system timer, 1ms step
uint32_t sys_timer_ms(void) {
	return MEM4(0x4023000c);
}

void sys_wait_ms(uint32_t delay) {
	uint32_t start = sys_timer_ms();
	while (sys_timer_ms() - start < delay);
}

uint32_t adi_read(uint32_t addr) {
	int32_t a;
	uint32_t base = 0x40600000;
	MEM4(base + 0x28) = addr;
	while ((a = MEM4(base + 0x2c)) < 0);
	return (uint16_t)a;
}

int adi_write(uint32_t addr, uint32_t val) {
	uint32_t base = 0x40600000;
	uint32_t t0 = sys_timer_ms(), n = 1000;
	while (MEM4(base + 0x30) & 0x800)
		if (sys_timer_ms() - t0 > 3 || !--n) return -1;
	MEM4(addr) = val;
	return 0;
}

#if !FDL2
static void init_pmu0(void) {
	MEM4(0x40410014) |= 1; // ANLG_WRAP_G6
	// PMU
	MEM4(0x402b009c) &= ~0x300;
	MEM4(0x402b009c) |= 0x100;
	MEM4(0x402b00a0) &= ~0x300;
	MEM4(0x402b00a0) |= 0x100;
}

static void init_pmu1(void) {
	uint32_t b = 0x402b0000; // PMU
	MEM4(b + 0x80) |= 0x33;
	MEM4(b + 0x84) &= ~0x13;
	MEM4(b + 0x8c) |= 0x33;
	MEM4(b + 0x90) &= ~0x13;
	MEM4(b + 0x94) |= 1;
	MEM4(b + 0x98) |= 0x10;
	MEM4(b + 0x9c) |= 2;
	MEM4(b + 0x9c) &= ~1;
	MEM4(b + 0xa0) |= 0x33;
	MEM4(b + 0xa8) &= ~0x20;
	MEM4(b + 0xac) |= 0x33;
}

static void init_dbg_freq(unsigned freq) {
	uint32_t a = MEM4(0x20e00038) & ~0x70707;
	uint32_t q = (freq - 1) / 500000000;
	MEM4(0x20e00038) = a | q | q << 8 | q << 16;
	MEM4(0x402d0270) |= 3;
}

static void init_mcu_freq(unsigned freq) {
	uint32_t a0 = MEM4(0x403f0000) & ~0xc0;
	uint32_t a1 = MEM4(0x403f0004) & 0xc0000000;
	static const unsigned tab[] = { 936, 1248, /* 1600 */ ~0 };
	unsigned i;
	uint32_t t0 = freq / 1000000, t1 = t0 / 26;

	t1 = (t1 & 0x7f) << 23 | ((t0 - t1 * 26) << 23) / 26;
	for (i = 0; tab[i] < t0;) i++;
	// ANLG_WRAP_G4
	MEM4(0x403f0000) = a0 | 5 | i << 6;
	MEM4(0x403f0004) = a1 | t1;
	DELAY(3200)
}

static void init_mcu(void) {
	unsigned freq = 1000000000;

	// AXI freq
	MEM4(0x402d0300) |= 3;
	DELAY(256)

	init_dbg_freq(freq);
	init_mcu_freq(freq);

	// APB freq
	MEM4(0x20e00054) = 3;
	DELAY(256)
	MEM4(0x21500020) |= 3;
	MEM4(0x402d0220) |= 3;
	MEM4(0x402d02f8) |= 1;
}

static void init_freq(void) {
	MEM4(0x402e00b0) |= 0x2800;
	// clk core
	MEM4(0x21500020) &= ~3;
	MEM4(0x402d0300) &= ~3;

	init_pmu0();
	init_pmu1();
	init_mcu();
	MEM4(0x402e2004) = 0x30;
}

static void init_uart(void) {
	uint32_t a, b = 0x70100000; // UART1 base
	unsigned baud = 115200;

	MEM4(b + 0x10) = 0;
	MEM4(0x71300000) |= 0x6000; // APB base
	a = (26000000 + (baud >> 1)) / baud;
	MEM4(b + 0x24) = a & 0xffff;
	MEM4(b + 0x28) = a >> 16;
	MEM4(b + 0x18) = 28;
	MEM4(b + 0x1c) = 0;
	MEM4(b + 0x20) = 0;
}

static void init_timer(void) {
	MEM4(0x402e0000) |= 0x400;
	MEM4(0x402e0010) |= 8;
}

static void init_pmu2(void) {
	uint32_t b = 0x402b0000; // PMU
	MEM4(b + 0x00) &= ~0xffffff;
	MEM4(b + 0x00) |= 0x11004;
	MEM4(b + 0x70) = 0x2222;
	MEM4(b + 0x74) = 0x101;
	MEM4(b + 0x18) &= ~0xffffff;
	MEM4(b + 0x18) |= 0x10106;
	MEM4(b + 0x28) &= ~0xffffff;
	MEM4(b + 0x28) |= 0x10204;
	MEM4(b + 0x34) &= ~0xffffff;
	MEM4(b + 0x34) |= 0x10402;
	MEM4(b + 0x38) &= ~0xffffff;
	MEM4(b + 0x38) |= 0x10501;
	MEM4(b + 0x3c) &= ~0xffffff;
	MEM4(b + 0x3c) |= 0x10303;
	MEM4(b + 0x44) &= ~0xffffff;
	MEM4(b + 0x44) |= 0x10105;
	MEM4(b + 0x5c) &= ~0xffffff;
	MEM4(b + 0x5c) |= 0x10101;
	MEM4(b + 0x78) = 0x8080808;
	MEM4(b + 0x7c) = 0x80800;
	MEM4(b + 0x17c) = 0x80808;
	MEM4(b + 0x180) = 0x800;
	MEM4(b + 0x338) = 0;
	MEM4(b + 0x33c) = 0x7F;
	MEM4(b + 0x340) = 1;
	MEM4(b + 0x344) = 0x10101;
	MEM4(b + 0x35c) = (MEM4(b + 0x35c) & 0xf) | 0x10;
}

static void init_adi(void) {
	MEM4(0x402e1000) = 0x10000;
	MEM4(0x40600008) = 0;
	MEM4(0x4060000c) = 0;
	MEM4(0x40600024) |= 0x80000000;
	MEM4(0x40600020) &= ~0x40000000;
}

static void init_adi_por(void) {
	adi_write(0x40608e38, adi_read(0x40608e38) & ~0x100);
	adi_write(0x40608e38, adi_read(0x40608e38) & ~1);
}

static void init_adi_wdg(void) {
	adi_write(0x40608c08, adi_read(0x40608c08) | 4); // wdg
	adi_write(0x40608c10, adi_read(0x40608c10) | 4); // wdg rtc
}
#endif

uint32_t chip_id;

uint32_t init_chip(void) {
	chip_id = MEM4(0x402e00fc);
#if FDL2
	return 0x80100000;
#else
	init_freq();
	init_uart();
	init_timer();
	init_pmu2();

	init_adi();
	init_adi_por();
	init_adi_wdg();

#if SDRAM_INIT == 1
	if (sdram_init()) for (;;);
#endif
	return 0x12000;
#endif
}

void entry_main() {
	dl_main();
}

