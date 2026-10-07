#include "common.h"

// SPI-NAND read via the NANDC with on-die ECC, ported from the boot ROM.

#define NANDC 0x21a00000                    // NAND controller base address
#define SCRATCH 0x80400000                  // DRAM buffer for the pages
#define NAND_PAGE 2048                      // page size
#define OOB 64                              // spare bytes per page
#define SPARE_SCRATCH (SCRATCH + NAND_PAGE) // DRAM buffer for spare/OOB
#define STAGE (SCRATCH + 0x1000)            // DRAM buffer for the reply data

// program the NANDC registers like the boot ROM does, partly not documented
static void nand_init(void) {
	// clock + reset (AP_AHB)
	MEM4(0x20e00000) |= 0x200;  // AP_AHB_EB: set bit9 -> NANDC AHB clock on
	MEM4(0x20e00004) &= ~0x70;  // AP_AHB_RST: clear bits[6:4] -> release NANDC reset
	MEM4(0x20e00000) &= ~0xd00; // AP_AHB_EB: clear bits 8/10/11 (other AHB gates)
	// AON clock-enables
	MEM4(0x402e0000) |= 0x100000; // AON_APB_EB0: set bit20 (NAND-side AON clock)
	MEM4(0x402e00b0) |= 0x1000;   // AON_APB_EB2: set bit12
	// NAND PHY pad mux
	MEM4(0x402a0024) |= 0x1000;   // IO_MUX ctrl: set bit12 (enable NAND pad group)
	for (uint32_t a = 0x402a0154; a < 0x402a0198; a += 4) MEM4(a) = 0;        // NAND pin-ctrl regs -> 0
	for (uint32_t a = 0x402a0554; a < 0x402a0598; a += 4) MEM4(a) = 0x300000; // NAND pad-drive regs -> 0x300000
	MEM4(0x402a0558) = 0x301000; // NF_CLE pad override
	MEM4(0x402a0574) = 0x300080; // NF_DQS pad override
	// AP_CKG: NAND clock
	MEM4(0x21500060) = 0x308; // NAND clock config = 0x308 (0x300 = SPI mode, 0x08 = source/div)
	MEM4(0x21500024) = 0x2;   // AP_CKG select = 2
	// NANDC timing / PHY / ECC-poly
	MEM4(NANDC + 0x14) = 0x3a493146; // TIMING0
	MEM4(NANDC + 0x18) = 0x192a0000; // TIMING1
	MEM4(NANDC + 0x34) = 0x81000000; // TIMEOUT
	MEM4(NANDC + 0x38) = 0x00030005; // CFG3
	MEM4(NANDC + 0xb0) = 0x00001004; // POLY0
	MEM4(NANDC + 0xb4) = 0x00001004; // POLY1
	MEM4(NANDC + 0xb8) = 0x00004013; // POLY2
	MEM4(NANDC + 0xbc) = 0x00400010; // POLY3
	MEM4(NANDC + 0xdc) = 0x00000006; // PHY_CFG
	MEM4(NANDC + 0xe0) = 0x00000100; // (undocumented)
	MEM4(NANDC + 0xe4) = 0x00000100; // (undocumented)
	MEM4(NANDC + 0xe8) = 0x000000bf; // (undocumented)
	MEM4(NANDC + 0x180) = 0x00000680; // (undocumented)
	MEM4(NANDC + 0x188) = 0x00000003; // (undocumented)
	MEM4(NANDC + 0x12c) = 0x0000548a; // (undocumented)
	// SPI-NAND geometry + timing
	MEM4(NANDC + 0x08) = 0x0a1f33ff; // CFG1: INTF_TYPE=3 (SPI), MAIN_SIZE=1K, 2 sectors, 32B spare
	MEM4(NANDC + 0x0c) = 0x2101000b; // CFG2
	MEM4(NANDC + 0xec) = 0x00040002; // DLL
	MEM4(NANDC + 0xf8) |= 0x2;       // CFG4: set bit1
}

// the page currently in SCRATCH
static uint32_t cur_page = ~0u;

// read one page into SCRATCH + its 64B spare right after it,
// returns the 0xC0 status (on-die ECC result) or -1 on timeout
static int nand_read_page(uint32_t page) {
	// .bss isn't cleared (it has random data in FDL2), so keep this in .data
	static int nand_ready __attribute__((section(".data"))) = 0;
	static int cur_status;
	uint16_t insts[] = {
		0x001f, 0x00b0, 0x0010, 0xe000, // SET_FEATURE: write cfg-reg 0xB0 = 0x10 -> on-die ECC enabled
		0x0013, page >> 16 & 0xff, page >> 8 & 0xff, page & 0xff, 0xe000, // page read -> load page into cache
		0x000f, 0x00c0, 0x1000, 0xe001, // GET_FEATURE: 0xC0 (status) -> wait until the load finished
		0x0003, NAND_PAGE >> 8, NAND_PAGE & 0xff, 0x6000, 0x5000, 0xe000, // READ-FROM-CACHE: col 2048 (spare)
		0x0003, 0x0000, 0x0000, 0x6000, 0x3000, 0xe000,                   // READ-FROM-CACHE: col 0 (main)
		0x000f, 0x00c0, 0x1000, 0xe200, // GET_FEATURE 0xC0 again -> copy the 0xC0 byte into STATUS0
		0xf000,                         // end of the micro-program
	};
	unsigned n = sizeof(insts) / sizeof(*insts), i;

	// the same page is requested again when reading in blocks < 2K
	if (page == cur_page) return cur_status;
	cur_page = ~0u;

	if (!nand_ready) {
		nand_init();
		nand_ready = 1;
	}

	MEM4(NANDC + 0x200) = 0;             // MAIN_ADDRH = 0 (main DMA target, high 32b)
	MEM4(NANDC + 0x204) = SCRATCH;       // MAIN_ADDRL = SCRATCH
	MEM4(NANDC + 0x208) = 0;             // SPAR_ADDRH = 0 (spare DMA target, high 32b)
	MEM4(NANDC + 0x20c) = SPARE_SCRATCH; // SPAR_ADDRL = SPARE_SCRATCH
	MEM4(NANDC + 0x210) = ~0u;           // STAT_ADDRH = ~0 (no move: don't write ECC-status to DRAM)
	MEM4(NANDC + 0x214) = ~0u;           // STAT_ADDRL = ~0
	// load the micro-insts into INSTRUCTION memory (0x220+), 2 per 32-bit word
	for (i = 0; i < n; i += 2)
		MEM4(NANDC + 0x220 + 2 * i) = insts[i] | (uint32_t)(i + 1 < n ? insts[i + 1] : 0) << 16;
	MEM4(NANDC + 0x04) = 0x01000070; // CFG0: MODE=auto, MAIN+SPAR apart, 2 sectors, controller ECC off

	MEM4(NANDC + 0x00) = 1;          // START = 1: run the micro-program
	for (i = 0; MEM4(NANDC) >> 31 & 1; i++) // poll START bit31 until done (busy wait)
		if (i >= 1000000) return -1;

	cur_status = MEM4(NANDC + 0x40) & 0xff; // STATUS0 = 0xC0 status (ECC result)
	cur_page = page;
	return cur_status;
}

// Read size bytes at offs, from the main area only (rec = 2048)
// or from pages with spare appended (rec = 2048 + 64).
// Returns the data (valid until the next call) or NULL on timeout.
// Pages with an uncorrectable ECC error are stored in bad[], terminated by ~0.
uint8_t *nand_read(uint32_t offs, uint32_t size, int oob, uint32_t bad[4]) {
	unsigned rec = oob ? NAND_PAGE + OOB : NAND_PAGE;
	uint8_t *dst = (uint8_t*)STAGE;

	while (size) {
		uint32_t page = offs / rec, col = offs % rec, n = rec - col;
		int fresh = page != cur_page;
		int st = nand_read_page(page);
		if (st < 0) return NULL;
		// report uncorrectable pages once: on the flash read or when a read starts at the page
		if ((st >> 4 & 7) == 2 && (fresh || !col)) *bad++ = page;
		if (n > size) n = size;
		memcpy(dst, (uint8_t*)SCRATCH + col, n);
		dst += n; offs += n; size -= n;
	}
	*bad = ~0u;
	return (uint8_t*)STAGE;
}
