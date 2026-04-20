#ifndef SYSCODE_H
#define SYSCODE_H

#define CHIPRAM_ADDR 0x40000000

void sys_wait_clk(uint32_t delay);

void sys_wait_us(uint32_t delay) {
	// delay *= 208;
	delay = (delay + delay * 4 + delay * 8) << 4;
	// SC6531: 312MHz
	if (_chip == 2) delay += delay >> 1;
	sys_wait_clk(delay);
}

// system timer, 1ms step
uint32_t sys_timer_ms(void) {
	uint32_t a, b = MEM4(0x8100300c);
	do a = b, b = MEM4(0x8100300c); while (a != b);
	return a;
}

void sys_wait_ms(uint32_t delay) {
	uint32_t start = sys_timer_ms();
	while (sys_timer_ms() - start < delay);
}

#endif // SYSCODE_H
