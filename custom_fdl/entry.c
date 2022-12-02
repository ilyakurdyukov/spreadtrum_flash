#include "common.h"

#if !CHIP || CHIP == 1
#define DO_SC6531E_INIT 1
#include "init_sc6531e.h"
#endif

#if !CHIP || CHIP == 2
#define DO_SC6531DA_INIT 1
#include "init_sc6531da.h"
#endif

#if CHIP == 0
int _chip = 0;
#endif

void entry_main() {

#if CHIP == 0
	{
		uint32_t a = MEM4(0x205003fc);
		if (a == 0) _chip = 1;	// SC6531E
		else if ((a ^ 0x65310000) >> 1 == 0) _chip = 2;	// SC6531DA
	}
#endif

#if DO_SC6531E_INIT
	if (_chip == 1) init_sc6531e();
#endif

#if DO_SC6531DA_INIT
	if (_chip == 2) init_sc6531da();
#endif

	dl_main();
}

