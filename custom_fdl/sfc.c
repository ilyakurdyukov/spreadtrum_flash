#include <string.h>
#include "sfc.h"
#include "common.h"
#include "syscode.h"

void sfc_init(void) {
	MEM4((uint32_t)SFC_BASE + 0x204) |= 3;
}

void sfc_cmdclr(sfc_base_t *sfc) {
	unsigned i;
	for (i = 0; i < 12; i++) sfc->cmd[i] = 0;
}

unsigned sfc_read_status(int cs) {
	sfc_base_t *sfc0 = SFC_BASE, *sfc;
	sfc = sfc0 + cs;

	sfc->tbuf_clr = 1;
	sfc->cmd[7] = 0;
	sfc0->cmd_set &= ~1; // write
	SFC_CMDSET(sfc, 1, 7);
	sfc->cmd[0] = 0x05; // Read Status Register
	sfc->type_info[0 / 4] = SFC_TYPEINFO(1, 1, WRITE, 0);
	sfc->type_info[7 / 4] = SFC_TYPEINFO(1, 1, READ, 0) << 24;
	sfc0->int_clr = 1 << cs;
	sfc0->soft_req |= 1;
	while (!(sfc->status & 1));

	return sfc->cmd[7] >> 24;
}

void sfc_write_enable(int cs) {
	sfc_base_t *sfc0 = SFC_BASE, *sfc;
	sfc = sfc0 + cs;
	do {
		sfc->tbuf_clr = 1;
		sfc0->cmd_set &= ~1; // write
		SFC_CMDSET(sfc, 1, 7);
		sfc->cmd[0] = 0x06; // Write Enable
		sfc->type_info[0 / 4] = SFC_TYPEINFO(1, 1, WRITE, 0);
		sfc0->int_clr = 1 << cs;
		sfc0->soft_req |= 1;
		while (!(sfc->status & 1));
	} while (!(sfc_read_status(cs) & 2));
}

void sfc_erase(int cs, int addr, int cmd, int addr_len) {
	uint32_t tmp;
	sfc_base_t *sfc0 = SFC_BASE, *sfc;
	sfc = sfc0 + cs;
	sfc_write_enable(cs);

	sfc->tbuf_clr = 1;
	sfc0->cmd_set &= ~1; // write
	SFC_CMDSET(sfc, 1, 7);
	sfc->cmd[0] = cmd;
	sfc->cmd[1] = addr;
	tmp = SFC_TYPEINFO(1, 1, WRITE, 0);
	if (addr_len)
		tmp |= SFC_TYPEINFO(1, addr_len, WRITE, 1) << 8;
	sfc->type_info[0] = tmp;
	sfc0->int_clr = 1 << cs;
	sfc0->soft_req |= 1;
	while (!(sfc->status & 1));

	sys_wait_us(30);
	// wait for completion
	while (sfc_read_status(cs) & 1);
}

void sfc_write(int cs, int addr, const void *buf, unsigned size) {
	const uint8_t *src = (const uint8_t*)buf, *end = src + size;
	sfc_base_t *sfc0 = SFC_BASE, *sfc;
	sfc = sfc0 + cs;

	while (src < end) {
		uint32_t tmp;
		sfc_write_enable(cs);

		sfc->tbuf_clr = 1;
		sfc0->cmd_set &= ~1; // write
		SFC_CMDSET(sfc, 1, 7);
		sfc->cmd[0] = addr >> 24 ? 0x12 : 0x02; // Page Program
		sfc->cmd[1] = addr;
		tmp = SFC_TYPEINFO(1, 1, WRITE, 0) |
				SFC_TYPEINFO(1, 3, WRITE, 1) << 8;
		if (addr >> 24) tmp += 1 << (3 + 8);
		sfc->type_info[0] = tmp;
		{
			unsigned k, n = end - src;
			// could be 40, but FDL use 32
			if (n > 32) n = 32;
			// don't allow to cross page boundary
			k = 256 - (addr & 255);
			if (n > k) n = k;
			addr += n;
			for (k = 2; n >= 4; k++, n -= 4, src += 4) {
				sfc->cmd[k] = src[0] | src[1] << 8 | src[2] << 16 | src[3] << 24;
				tmp = SFC_TYPEINFO(1, 4, WRITE, 0);
				sfc->type_info[k >> 2] |= tmp << (k & 3) * 8;
			}
			if (n) {
				uint32_t a = src[0];
				if (n >= 2) a |= src[1] << 8;
				if (n > 2) a |= src[2] << 16;
				sfc->cmd[k] = a; src += n;
				tmp = SFC_TYPEINFO(1, n, WRITE, 0);
				sfc->type_info[k >> 2] |= tmp << (k & 3) * 8;
			}
		}
		sfc0->int_clr = 1 << cs;
		sfc0->soft_req |= 1;
		while (!(sfc->status & 1));

		sys_wait_us(30);
		// wait for completion
		while (sfc_read_status(cs) & 1);
	}
}

/* return to reading mode */
void sfc_spiread(int cs) {
	sfc_base_t *sfc0 = SFC_BASE, *sfc;
	sfc = sfc0 + cs;

	sfc->tbuf_clr = 1;
	sfc_cmdclr(sfc);
	sfc0->cmd_set |= 1; // read
	SFC_CMDSET(sfc, 1, 7);
	sfc->cmd[0] = 0x03;
	sfc->type_info[0] =
			SFC_TYPEINFO(1, 1, WRITE, 0) |
			SFC_TYPEINFO(1, 3, WRITE, 1) << 8;
	sfc0->int_clr = 1 << cs;
	//sfc->clk = 2;
}

