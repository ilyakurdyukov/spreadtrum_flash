#ifndef SFC_H
#define SFC_H

#include <stdint.h>

/* Serial Flash Controller */
#define SFC_BASE ((sfc_base_t*)0x20a00000)

typedef volatile struct {
	uint32_t cmd_set, soft_req, tbuf_clr, int_clr;
	uint32_t status, timing, rd_sample, clk;
	uint32_t cs, lsb, io_dly, wp_hld_init;
	uint32_t dl0, dl1, dll, dummy_3c;
	uint32_t cmd[12], type_info[3], dummy_7c;
} sfc_base_t;

enum {
	SFC_BIT_MODE1 = 0,
	SFC_BIT_MODE2 = 1,
	SFC_BIT_MODE4 = 2,

	SFC_OP_WRITE = 0,
	SFC_OP_READ = 1,
	SFC_OP_HIGHZ = 2,
};

void sfc_init(void);
void sfc_cmdclr(sfc_base_t *sfc);
unsigned sfc_read_status(int cs);
void sfc_write_enable(int cs);
void sfc_erase(int cs, int addr, int cmd, int addr_len);
void sfc_write(int cs, int addr, const void *buf, unsigned size);
void sfc_spiread(int cs);

#define SFC_CMDSET(sfc, bit, cmdbuf) do { \
	sfc->cmd_set = (sfc->cmd_set & ~0x1e) | (SFC_BIT_MODE##bit << 1 | (7 - cmdbuf) << 3); \
} while (0)

#define SFC_TYPEINFO(bit, num, op, send) \
	(1 | SFC_BIT_MODE##bit << 1 | (num - 1) << 3 | SFC_OP_##op << 5 | send << 7)

#endif

