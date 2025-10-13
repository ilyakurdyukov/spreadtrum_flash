#include "common.h"

// function names restored from debug prints and sources for old chips

static inline uint32_t insert_bits(uint32_t orig, unsigned sh, unsigned n, unsigned val) {
	uint32_t m = (1 << n) - 1;
	return (orig & ~(m << sh)) | (val & m) << sh;
}

static inline void insert_bits_mem(uint32_t addr, unsigned sh, unsigned n, unsigned val) {
	MEM4(addr) = insert_bits(MEM4(addr), sh, n, val);
}

static void dmc_print_str(const char *s) {
	while (*s) {
		while (MEM4(0x7010000c) & 0xff00);
		MEM4(0x70100000) = *s++;
	}
}

static const unsigned ddr_clk_array[] = { 256, 384, 533 };

#define DDR_DLL_CFG 8
#define DDR_DRV_CFG 15
#define DRAM_LPDDR2 0x101

typedef struct {
	unsigned freq;
	uint32_t data[14];
} local_timing_t;

static int ui_qrt_dly_data[3];

static const local_timing_t dmc_local_timing_lpddr2[] = {
	{ 256, {
		0x42060404, 0x03070501, 0x34182039, 0x31080c0a,
		0x00070000, 0x000f7700, 0x000f0000, 0x000c0000,
		0x003f0002, 0x003f0002, 0x003c0002, 0x01a83f02,
		0x07d21808, 0x00060066 } },
	{ 384, {
		0x43090606, 0x03080700, 0x4f232c55, 0x5208131d,
		0x000a0101, 0x001e7700, 0x001e0000, 0x00180000,
		0x00ff0004, 0x00ff0004, 0x00fc0004, 0x02583d04,
		0x0bc28808, 0x00090099 } },
	{ 533, {
		0x250c0909, 0x03090802, 0x2e303836, 0x73081a16,
		0x000c0202, 0x001e7701, 0x001e0001, 0x00180001,
		0x00ff0006, 0x00ff0006, 0x00f80006, 0x03283d06,
		0x10428808, 0x000d00d5 } }
};

static void sdram_clk_init(unsigned freq) {
	int a = 9;
	if (freq == 384) a = 11;
	if (freq == 533) a = 12;
	insert_bits_mem(0x402e008c, 11, 8, a);
	insert_bits_mem(0x402e00ac, 0, 1, 1);
	insert_bits_mem(0x402e008c, 0, 1, 1);
	insert_bits_mem(0x402e008c, 0, 1, 0);
}

static uint32_t dmc_sprd_delay(unsigned n) {
	uint32_t i, a;
	for (i = 0; i < n * 2; i++) {
		a = MEM4(0x402b00c4);
		DELAY(5);
	}
	return a;
}

static void dmc_zq_cal(int target_drv) {
	uint32_t tmp;
	unsigned inc, deinc, drvn, drvp;

	tmp = MEM4(0x30000008) & ~(1 << 16);
	tmp = insert_bits(tmp, 10, 5, target_drv);
	MEM4(0x30000008) = tmp;
	dmc_sprd_delay(2);

	// drvn
	inc = 0;
	tmp = MEM4(0x30000008) & ~(1 << 15);
	tmp = insert_bits(tmp, 0, 5, inc);
	MEM4(0x30000008) = tmp;
	while (MEM4(0x30000008) & 1 << 17) {
		tmp = insert_bits(tmp, 0, 5, ++inc);
		MEM4(0x30000008) = tmp;
		dmc_sprd_delay(1);
	}
	deinc = 31;
	tmp = MEM4(0x30000008) & ~(1 << 15);
	tmp = insert_bits(tmp, 0, 5, deinc);
	MEM4(0x30000008) = tmp;
	while (!(MEM4(0x30000008) & 1 << 17)) {
		tmp = insert_bits(tmp, 0, 5, --deinc);
		MEM4(0x30000008) = tmp;
		dmc_sprd_delay(1);
	}
	drvn = (inc + deinc) >> 1;
	tmp = MEM4(0x30000008);
	tmp = insert_bits(tmp, 0, 5, drvn);
	MEM4(0x30000008) = tmp;

	// drvp
	inc = 0;
	tmp |= 1 << 15;
	tmp = insert_bits(tmp, 5, 5, inc);
	MEM4(0x30000008) = tmp;
	while (!(MEM4(0x30000008) & 1 << 17)) {
		tmp = insert_bits(tmp, 5, 5, ++inc);
		MEM4(0x30000008) = tmp;
		dmc_sprd_delay(1);
	}
	deinc = 31;
	tmp = MEM4(0x30000008) | 1 << 15;
	tmp = insert_bits(tmp, 5, 5, deinc);
	MEM4(0x30000008) = tmp;
	while (MEM4(0x30000008) & 1 << 17) {
		tmp = insert_bits(tmp, 5, 5, --deinc);
		MEM4(0x30000008) = tmp;
		dmc_sprd_delay(1);
	}
	drvp = (inc + deinc) >> 1;
	tmp = MEM4(0x30000008);
	MEM4(0x30000008) = insert_bits(tmp, 5, 5, drvp);
	MEM4(0x30000008) |= 1 << 16;

	if (0) {
		tmp = MEM4(0x30000008);
		drvn = tmp & 0x1f;
		drvp = (tmp >> 5) & 0x1f;
	}

	tmp = MEM4(0x30000390) | 1 << 21;
	tmp = insert_bits(tmp, 0, 5, drvp);
	tmp = insert_bits(tmp, 8, 5, drvn);
	MEM4(0x30000390) = tmp;

	tmp = MEM4(0x30000490) | 1 << 21;
	tmp = insert_bits(tmp, 0, 5, drvp);
	tmp = insert_bits(tmp, 8, 5, drvn);
	MEM4(0x30000490) = tmp;
}

static int search_for_freq_point(const local_timing_t *p, int n, unsigned freq) {
	int i;
	for (i = 0; i < n; i++)
		if (p[i].freq == freq) return i;
	return -1;
}

static int lpddr_timing_pre_init(int type, const unsigned *freq_arr) {
	int i, j, k;
	const local_timing_t *p = dmc_local_timing_lpddr2;

	if (type != DRAM_LPDDR2) return -1;
	for (i = 0; i < 3; i++) {
		uint32_t b = 0x30000200 + (i << 6);
		j = search_for_freq_point(p, 3, freq_arr[i]);
		if (j < 0) break;
		for (k = 0; k < 14; k++, b += 4)
			MEM4(b) = p[j].data[k];
	}
	return -1;
}

static int dram_ddr_init_pre_setting(unsigned freq) {
	uint32_t tmp;
	int a;

	tmp = MEM4(0x30000004);
	tmp = insert_bits(tmp, 0, 3, 1);
	tmp = insert_bits(tmp, 16, 3, 1);
	tmp = insert_bits(tmp, 20, 6, 0);
	MEM4(0x30000004) = tmp;

	insert_bits_mem(0x3000000c, 0, 15, 0);
	insert_bits_mem(0x30000100, 17, 1, 1);

	lpddr_timing_pre_init(DRAM_LPDDR2, ddr_clk_array);

	a = 0; // freq == 256
	switch (freq) {
		case 256: a = 0; break;
		case 384: a = 1; break;
		case 533: a = 2; break;
		default: return -1;
	}
	insert_bits_mem(0x3000012c, 4, 2, a);

	tmp = MEM4(0x30000000);
	tmp = insert_bits(tmp, 0, 3, 1);
	tmp = insert_bits(tmp, 14, 2, 1);
	tmp = insert_bits(tmp, 4, 3, 2);
	MEM4(0x30000000) = tmp;

	tmp = MEM4(0x30000100);
	tmp = insert_bits(tmp, 8, 1, 0);
	tmp = insert_bits(tmp, 17, 1, 1);
	tmp = insert_bits(tmp, 7, 1, 0);
	tmp = insert_bits(tmp, 4, 3, 2);
	MEM4(0x30000100) = tmp;
	return 0;
}

static int get_clk_mode(void) {
	int i = MEM4(0x3000012c) >> 4 & 3;
	return MEM4(0x30000230 + (i << 6)) >> 15 & 1;
}

static int get_half_mode(void) {
	int i = MEM4(0x3000012c) >> 4 & 3;
	return MEM4(0x3000022c + (i << 6)) >> 8 & 1;
}

static void set_half_mode(int mode) {
	int i = MEM4(0x3000012c) >> 4 & 3;
	insert_bits_mem(0x3000022c + (i << 6), 8, 1, mode);
}

static int dmc_dll_init(int cfg);

static void ui_qtr_dly_cnt_init(void) {
	int i, clk_mode, old_mode = get_half_mode();
	uint32_t tmp;

	set_half_mode(0);
	dmc_dll_init(DDR_DLL_CFG);
	dmc_sprd_delay(10);

	clk_mode = get_clk_mode();
	for (i = 0; i < 3; i++) {
		tmp = MEM4(0x30000304 + (i << 8)) & 0x7f;
		if (clk_mode == 1) tmp >>= 1;
		ui_qrt_dly_data[i] = tmp << 2;
	}
	set_half_mode(old_mode);
}

static void lpddr_dll_init(int type) {
	(void)type;
	MEM4(0x30000308) = 0x80000008;
	MEM4(0x30000300) = 0x6a22c401;

	MEM4(0x30000408) = 0x80000008;
	MEM4(0x3000040c) = 0x80000006;
	MEM4(0x30000410) = 0x80000006;
	MEM4(0x30000400) = 0x6a22c401;

	MEM4(0x30000508) = 0x80000008;
	MEM4(0x3000050c) = 0x80000006;
	MEM4(0x30000510) = 0x80000006;
	MEM4(0x30000500) = 0x6a22c401;
}

static int dmc_dll_init(int cfg) {
	uint32_t tmp, b = 0x30000000;

	lpddr_dll_init(DRAM_LPDDR2);
	while (!(MEM4(b + 0x304) & MEM4(b + 0x404) & MEM4(b + 0x504) & 0x10000000));

	tmp = MEM4(b + 0x300);
	tmp = insert_bits(tmp, 9, 2, 3);
	tmp = insert_bits(tmp, 12, 1, 1);
	tmp = insert_bits(tmp, 14, 2, 3);
	tmp = insert_bits(tmp, 27, 1, 1);
	tmp = insert_bits(tmp, 0, 7, cfg);
	MEM4(b + 0x300) = tmp;
	MEM4(b + 0x400) = tmp;
	MEM4(b + 0x500) = tmp;
	return 0;
}

static void dmc_mrw(int cs, int mr_addr, int val) {
	uint32_t tmp;
#if 1
	insert_bits_mem(0x30000108, 0, 8, val);
#else // buggy orig code
	MEM4(0x30000108) = val;
#endif

	tmp = MEM4(0x30000104);
	if (cs == 0) {
		tmp |= 1 << 31;
		tmp &= ~(1 << 28);
	} else if (cs == 1) {
		tmp &= ~(1 << 31);
		tmp |= 1 << 28;
	} else { // cs == 2
		tmp |= 1 << 31;
	}
	tmp |= 1 << 24;
	tmp = insert_bits(tmp, 0, 16, mr_addr);
	MEM4(0x30000104) = tmp;
}

static int lpddr_powerup_init(unsigned freq) {
	uint32_t tmp;

	MEM4(0x3000000c) = 0;
	MEM4(0x30000100) |= 1 << 14;
	dmc_sprd_delay(500);
	dmc_mrw(3, 63, 0);
	dmc_sprd_delay(10);
	dmc_mrw(0, 10, 0xff);
	dmc_mrw(1, 10, 0xff);
	dmc_sprd_delay(500);
	MEM4(0x30000490) |= 3 << 24;

	if (freq == 533) tmp = 6;
	else if (freq == 384) tmp = 4;
	else tmp = 2; // freq == 256
	dmc_mrw(0, 2, tmp);
	MEM4(0x30000108) = tmp << 5 | 3; // 0x43, 0x83, 0xc3
	MEM4(0x30000104) = 0x81000001;
	MEM4(0x3000000c) = 0x7fff;
	MEM4(0x30000104) = 0x80200000;
	while (MEM4(0x30000104) >> 20 & 0x7f);
	MEM4(0x30000104) = 0x80200000;
	while (MEM4(0x30000104) >> 20 & 0x7f);
	return 0;
}

static void dmc_init_post_setting(unsigned freq) {
	uint32_t tmp;
	(void)freq;

	insert_bits_mem(0x30000234, 0, 16, 1024);
	insert_bits_mem(0x30000274, 0, 16, 1536);
	insert_bits_mem(0x300002b4, 0, 16, 2132);

	tmp = MEM4(0x30000124);
	tmp = insert_bits(tmp, 16, 4, 15);
	tmp = insert_bits(tmp, 0, 3, 5);
	tmp = insert_bits(tmp, 4, 4, 15);
	tmp = insert_bits(tmp, 8, 4, 15);
	MEM4(0x30000124) = tmp;

	tmp = MEM4(0x30000114);
	tmp = insert_bits(tmp, 0, 24, 0x1ff0);
	tmp = insert_bits(tmp, 24, 1, 0);
	MEM4(0x30000114) = tmp;

	tmp = MEM4(0x30000118);
	tmp = insert_bits(tmp, 26, 2, 2);
	tmp = insert_bits(tmp, 24, 1, 0);
	MEM4(0x30000118) = tmp;

	tmp = MEM4(0x3000010c);
	tmp = insert_bits(tmp, 12, 1, 1);
	tmp = insert_bits(tmp, 15, 1, 0);
	MEM4(0x3000010c) = tmp;

	tmp = MEM4(0x3000012c);
	tmp = insert_bits(tmp, 0, 4, 15);
	tmp = insert_bits(tmp, 17, 1, 1);
	tmp = insert_bits(tmp, 20, 12, 0);
	MEM4(0x3000012c) = tmp;

	insert_bits_mem(0x3000000C, 0, 15, 0x7fff);
	insert_bits_mem(0x30000100, 16, 1, 1);
	dmc_sprd_delay(1);
	insert_bits_mem(0x30000100, 16, 1, 0);
	insert_bits_mem(0x30000000, 4, 6, 3);
	dmc_mrw(2, 1, 35);
	insert_bits_mem(0x30000000, 8, 1, 1);
	insert_bits_mem(0x30000100, 12, 1, 1);

	insert_bits_mem(0x402b00d0, 6, 1, 1);
	insert_bits_mem(0x402b00d0, 2, 1, 0);
}

static void dmc_port_qos_cfg0(uint32_t addr, int a, int b, int c) {
	uint32_t tmp;
	tmp = MEM4(addr);
	tmp = insert_bits(tmp, 0, 4, a);
	tmp = insert_bits(tmp, 16, 8, b);
	tmp = insert_bits(tmp, 24, 8, c);
	MEM4(addr) = tmp;
}

static void dmc_port_qos_cfg1(uint32_t addr, int a, int b, int c) {
	uint32_t tmp;
	tmp = MEM4(addr);
	tmp = insert_bits(tmp, 0, 4, a);
	tmp = insert_bits(tmp, 16, 10, b);
	tmp = insert_bits(tmp, 31, 1, c);
	MEM4(addr) = tmp;
}

static const uint8_t port_para[8][3] = {
	{ 7, 0, 0 }, { 7, 0, 0 },
	{ 0xe, 0x44, 0x44 }, { 0xe, 0, 0 },
	{ 0xd, 0x44, 0x44 }, { 0xd, 0, 0 },
	{ 9, 0, 0 }, { 9, 0, 0 }
};

static void dmc_pub_qos_init(void) {
	uint32_t tmp;
	unsigned i;

	for (i = 0; i < 8; i += 2)
		dmc_port_qos_cfg0(0x30000020 + i * 4, port_para[i][0], port_para[i][1], port_para[i][2]);
	for (i = 1; i < 8; i += 2)
		dmc_port_qos_cfg1(0x30000020 + i * 4, port_para[i][0], port_para[i][1], port_para[i][2]);

	tmp = MEM4(0x30000000);
	tmp = insert_bits(tmp, 16, 11, 400);
	tmp = insert_bits(tmp, 27, 1, 0);
	tmp = insert_bits(tmp, 9, 2, 3);
	MEM4(0x30000000) = tmp;

	MEM4(0x30000128) = 0xc0003000;
	MEM4(0x300e32ec) = 0xccadcc55;
	MEM4(0x20e000a4) = 0x55005566;
	MEM4(0x20e000b8) = 0x55550055;
	MEM4(0x20e000bc) = 0xff550000;

	insert_bits_mem(0x20e03058, 0, 24, 0x555555);
	insert_bits_mem(0x60830074, 12, 4, 9);

	tmp = MEM4(0x60830078);
	tmp = insert_bits(tmp, 20, 4, 12);
	tmp = insert_bits(tmp, 12, 4, 9);
	tmp = insert_bits(tmp, 0, 4, 9);
	MEM4(0x60830078) = tmp;

	insert_bits_mem(0x6083007c, 12, 4, 9);

	tmp = MEM4(0x60830080);
	tmp = insert_bits(tmp, 20, 4, 12);
	tmp = insert_bits(tmp, 12, 4, 9);
	tmp = insert_bits(tmp, 0, 4, 9);
	MEM4(0x60830080) = tmp;
}

static uint32_t trans_addr_to_jedec_addr(uint32_t off) {
	unsigned dw = 16, cols = 1 << 10, banks = 8, rows = 1 << 12;
	unsigned col, bank, row, cs = 0;

	off /= dw / 8;
	col = off % cols; off /= cols;
	bank = off % banks; off /= banks;
	row = off; (void)rows;

	return row << 16 | cs << 15 | bank << 12 | col;
}

static const uint32_t bist_pattern[] = {
	0xeeef7bde, 0x4210db6e, 0x92488888,
	0xc7878787, 0x33331c71, 0x55, 0, 0
};

static void sipi_bist_set_pattern(const uint32_t *src) {
	int i;
	for (i = 0; i < 8; i++)
		MEM4(0x3000019c + i * 4) = src[i];
}

static int sipi_bist_types_test(int type) {
	uint32_t tmp, seed = 0;
	uint32_t chip_size = 64 << 20;
	unsigned n, len = 0x40000, opmode = 0, burst;

	if (len > chip_size) len = chip_size;

	tmp = MEM4(0x30000000) >> 4 & 7;
	if (tmp > 4) return -3;
	burst = 1 << tmp;

	MEM4(0x30000198) = seed;
	// sipi_bist_set_pattern(bist_pattern);
	MEM4(0x30000184) = len >> 2;
	MEM4(0x30000188) = 0;
	tmp = trans_addr_to_jedec_addr(chip_size - len);
	tmp = 0; // jedec_addr doesn't work, always starts from 0 or fails
	// the original code also fails to put the test range at the end
	MEM4(0x3000018c) = tmp; // start addr

	tmp = MEM4(0x30000180);
	tmp |= 1 << 13;
	tmp |= 1; // on
	tmp = insert_bits(tmp, 16, 10, burst);
	MEM4(0x30000180) = tmp;
	tmp &= ~(1 << 13);
	tmp = insert_bits(tmp, 8, 2, type);
	MEM4(0x30000180) = tmp;

	tmp = MEM4(0x30000180);
	tmp = insert_bits(tmp, 12, 2, 0);
	tmp = insert_bits(tmp, 4, 2, opmode);
	tmp = insert_bits(tmp, 0, 2, 3);
	MEM4(0x30000180) = tmp;

	n = (MEM4(0x30000184) - 1) * 2;
	while (n > 0 && !(MEM4(0x30000180) & 4)) n--;

	MEM4(0x30000180) &= ~1; // off
	if (n < 1) return -2;
	return MEM4(0x30000180) & 8 ? 0 : -1;
}

static int sipi_bist_simple_test(void) {
	return sipi_bist_types_test(1);
}

static void reset_dmc_fifo(void) {
	MEM4(0x30000100) |= 0x10000;
	MEM4(0x30000100) &= ~0x10000;
}

static void dqin_bit_offset_adjust(uint32_t x) {
	x &= 0x1f; x |= x << 8; x |= x << 16;
	MEM4(0x30000420) = x;
	MEM4(0x30000520) = x;
	MEM4(0x30000620) = x;
	MEM4(0x30000720) = x;
	MEM4(0x30000424) = x;
	MEM4(0x30000524) = x;
	MEM4(0x30000624) = x;
	MEM4(0x30000724) = x;
}

static void dqs_right_shift(unsigned x) {
	if (x > 31) x = 31;
	insert_bits_mem(0x30000428, 0, 5, x);
	insert_bits_mem(0x30000528, 0, 5, x);
	insert_bits_mem(0x30000628, 0, 5, x);
	insert_bits_mem(0x30000728, 0, 5, x);
}

static void set_dqs_dll_with_offset(uint32_t *dst, uint32_t *def, int *mid, int init) {
	uint32_t tmp, b;
	int i, half_mode = get_half_mode();

	for (i = 0; i < 2; i++) {
		int off, sign, cnt, delay = mid[i] - init;

		tmp = def[i];
		cnt = 4 * ((tmp >> 8) & 0x7f);
		if (!half_mode) cnt >>= 1;

		if (cnt > delay)
			off = cnt - delay, sign = 3;
		else
			off = delay - cnt, sign = 2;

		tmp = insert_bits(tmp, 30, 2, sign);
		tmp = insert_bits(tmp, 16, 7, off >> 2);
		tmp = insert_bits(tmp, 28, 2, off);
		MEM4(dst[i]) = tmp;

		b = 0x30000400 + i * 0x100;
		MEM4(b) |= 0x800;
		dmc_sprd_delay(10);
		MEM4(b) &= ~0x800;
		reset_dmc_fifo();
		MEM4(dst[i]); // dummy read
	}
}

static const uint32_t bist_mask[3][2] = {
	{ 0xff00ff00, 0x00ff00ff }, // wde
	{ 0xffffff00, 0xffff00ff }, // rde pos
	{ 0xff00ffff, 0x00ffffff }  // rde neg
};

static int dmc_lpddr3_wde_training(void) {
	int i, j, n, err = 0;
	uint32_t regs[] = { 0x30000408, 0x30000508 };
	uint32_t old_bist5 = MEM4(0x30000194);
	uint32_t vals[2];
	int mid[2];

	vals[0] = MEM4(0x30000408);
	vals[1] = MEM4(0x30000508);

	for (i = 0; i < 2; i++) {
		int first = -1, last = -1;

		MEM4(0x30000194) = bist_mask[0][i];
		dqs_right_shift(31);
		n = (ui_qrt_dly_data[1 + i] >> 2) + 8;

		for (j = 0; j < n; j++) {
			int k = j;
			if (k >= n - 8) {
				dqs_right_shift(0);
				k -= 8;
			}
			MEM4(regs[i]) = k;
			reset_dmc_fifo();
			if (sipi_bist_types_test(2)) {
				if (first >= 0) {
					if ((j - 1) - first > 16) break;
					first = -1;
				}
			} else if (first < 0) first = j;
		}
		last = j - 1;
		MEM4(regs[i]) = vals[i];
		if (first < 0) { err = -1; break; }
		reset_dmc_fifo();
		mid[i] = (first + last) * 4 / 2 - 32;
		// dmc_wde_mid[i] = mid[i] / 4;
	}
	MEM4(0x30000194) = old_bist5;
	if (!err) set_dqs_dll_with_offset(regs, vals, mid, 0);
	dqs_right_shift(0);
	reset_dmc_fifo();
	return 0; // return err?
}

static int dmc_lpddr3_rde_training(int neg) {
	int i, j, n, err = 0;
	uint32_t regs[] = { 0x3000040c + neg * 4, 0x3000050c + neg * 4 };
	uint32_t old_bist5 = MEM4(0x30000194);
	uint32_t vals[2];
	int mid[2];

	vals[0] = MEM4(regs[0]);
	vals[1] = MEM4(regs[1]);
	dqin_bit_offset_adjust(28);

	for (i = 0; i < 2; i++) {
		int first = -1, last = -1;

		MEM4(0x30000194) = bist_mask[1 + neg][i];
		n = 128;

		for (j = 0; j < n; j++) {
			MEM4(regs[i]) = j;
			reset_dmc_fifo();
			if (sipi_bist_types_test(2)) {
				if (first >= 0) {
					if ((j - 1) - first > 16) break;
					first = -1;
				}
			} else if (first == -1) first = j;
		}
		last = j - 1;
		MEM4(regs[i]) = vals[i];
		if (first < 0 || last - first < 16) { err = -1; break; }
		reset_dmc_fifo();
		mid[i] = (first + last) * 4 / 2;
		// dmc_rde_mid[neg][i] = mid[i] / 4;
	}
	MEM4(0x30000194) = old_bist5;
	dqin_bit_offset_adjust(0);
	if (!err) set_dqs_dll_with_offset(regs, vals, mid, 28);
	reset_dmc_fifo();
	return 0; // return err?
}

static int ddr_scan_online(unsigned freq) {
	if (sipi_bist_simple_test())
		dmc_print_str("sipi_bist_simple_test first failed\n");
	if (freq == ddr_clk_array[2]) {
		int i;
		for (i = 7; i < 14; i++)
			MEM4(0x30000180 + i * 4) = 0xffff;
		if (dmc_lpddr3_wde_training()) {
			dmc_print_str("dmc_lpddr3_wde_training failed\n");
			return -1;
		}
		if (dmc_lpddr3_rde_training(0)) {
			dmc_print_str("dmc_lpddr3_rde_training_pos failed\n");
			return -1;
		}
		if (dmc_lpddr3_rde_training(1)) {
			dmc_print_str("dmc_lpddr3_rde_training_neg failed\n");
			return -1;
		}
		sipi_bist_set_pattern(bist_pattern);
		if (sipi_bist_simple_test()) {
			dmc_print_str("sipi_bist_simple_test last failed\n");
			return -1;
		}
	}
	return 0;
}

// hangs when inlined with GCC + LTO
__attribute__((noinline))
int sdram_init(void) {
	unsigned ddr_clk = ddr_clk_array[2];
	uint32_t tmp;

	MEM4(0x402b0130) = 0;

	dmc_print_str("DDR init start\n");
	dmc_print_str("sdram_clk_init begin\n");
	sdram_clk_init(ddr_clk);
	dmc_zq_cal(DDR_DRV_CFG);
	if (dram_ddr_init_pre_setting(ddr_clk)) return -3;
	if (ddr_clk == ddr_clk_array[2]) ui_qtr_dly_cnt_init();
	if (dmc_dll_init(DDR_DLL_CFG)) return -4;
	dmc_print_str("dmc_dll_init end\n");
	if (lpddr_powerup_init(ddr_clk)) return -5;
	dmc_print_str("lpddr_powerup_init end\n");
	dmc_init_post_setting(ddr_clk);
	dmc_print_str("dmc_init_post_setting end\n");
	dmc_pub_qos_init();
	dmc_print_str("qos init end\n");

	tmp = adi_read(0x40608e30);
	if (!(tmp & 0x1000) || (tmp & 0x80))
		if (!(adi_read(0x40608050) & 8))
			if (ddr_scan_online(ddr_clk)) return -6;

	MEM4(0x30000100) |= 0x1000;
	MEM4(0x402b2230) = 0x400;
	MEM4(0x402b0130) = 1;
	MEM4(0x402b0250) |= 1;
	dmc_print_str("DDR init OK\n");
	return 0;
}

