#include "common.h"

#if !CHIP || CHIP == 1
#define DO_SC6531E_INIT 1
#include "init_sc6531e.h"
#endif

#if CHIP == 0
int chip_id = 0;
#endif

void entry_main() {

#if CHIP == 0
	{
		uint32_t a = MEM4(0x205003fc);
		if (a == 0) chip_id = 1;	// SC6531E
		else if ((a ^ 0x65300000) >> 17 == 0) chip_id = 2;	// SC6531DA
	}
#endif

#if DO_SC6531E_INIT
	if (chip_id == 1) init_sc6531e();
#endif

	dl_main();
}

