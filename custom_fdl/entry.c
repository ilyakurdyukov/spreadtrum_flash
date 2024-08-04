#include "common.h"

#if !CHIP || CHIP == 1
#define DO_SC6531E_INIT 1
#include "init_sc6531e.h"
#endif

#if !CHIP || CHIP == 2
#define DO_SC6531DA_INIT 1
#include "init_sc6531da.h"
#endif

#if !CHIP || CHIP == 3
#define DO_SC6530_INIT 1
#include "init_sc6530.h"
#endif

#if !CHIP
int _chip = 0;
#endif

uint32_t chip_id;

static void init_chip_id(void) {
	uint32_t t0 = 0, t1;
	if (!CHIP || CHIP == 2 || CHIP == 3) {
		t0 = MEM4(0x205003fc);
#if !CHIP
		t1 = (t0 ^ 0x65300000) >> 16;
		if (t1 == 0) _chip = 3;	// SC6530
		if (t1 == 1) _chip = 2; // SC6531DA
#endif
	}
#if !CHIP || CHIP == 1
	if (t0 == 0) {
		t1 = MEM4(0x8b00035c);	// 0x36320000
		t0 = MEM4(0x8b000360);	// 0x53433635
		t0 = (t0 >> 8) << 28 | (t0 << 28) >> 4;
		t0 |= (t1 & 0xf000000) >> 4 | (t1 & 0xf0000);
		t1 = MEM4(0x8b000364);	// 0x00000001
		t0 |= t1;
#if !CHIP
		if ((t0 ^ 0x65620000) >> 16 == 0) _chip = 1;	// SC6531E
#endif
	}
#endif
	chip_id = t0;
}

void entry_main() {
	init_chip_id();

#if !CHIP
	if (!_chip) for (;;);
#endif

#if DO_SC6531E_INIT
	if (_chip == 1) init_sc6531e();
#endif

#if DO_SC6531DA_INIT
	if (_chip == 2) init_sc6531da();
#endif

#if DO_SC6530_INIT
	if (_chip == 3) init_sc6530();
#endif

	// SFC_CS1_START_ADDR:
	// default is 4(MB), which prevents reading full flash from cs0
	if (_chip != 1) MEM4(0x20a00200) = 16;

	dl_main();
}

