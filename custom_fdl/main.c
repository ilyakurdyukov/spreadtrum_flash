#include "common.h"
#include "cmd_def.h"
#include "packet.h"
#include "channel.h"

#if WITH_SFC
#include "sfc.c"
#define ERASE_CMD 0x20
#define ERASE_BLK 0x1000
#endif

#if FDL_DEBUG
struct _IO_FILE;
typedef struct _IO_FILE FILE;
extern FILE *stdout;
int fputc(int, FILE*);
#include <stdarg.h>
#define PREFIX(name) name
// fpdoom/libc/printf.h
#include "printf.h"

__attribute__((unused))
static void fdl_printf(const char *fmt, ...) {
	uint8_t *pkt = dl_send_buf();
	int len;
	va_list va;
	va_start(va, fmt);
	len = vsnprintf((char*)pkt + 4, MAX_PKT_PAYLOAD, fmt, va);
	WRITE16_BE(pkt, BSL_REP_LOG);
	WRITE16_BE(pkt + 2, len);
	dl_send_packet(pkt);
}
#define DBG_LOG(...) fdl_printf(__VA_ARGS__)
#else
#define DBG_LOG(...) (void)0
#endif

static struct {
	uintptr_t start;
	uint32_t size, recv;
} dl_status;

#define FW_ADDR (_chip != 1 ? 0x30000000 : 0x10000000)

static int data_start(uint8_t *pkt) {
	unsigned len = READ16_BE(pkt + 2);
	uint32_t addr = READ32_BE(pkt + 4);
	uint32_t size = READ32_BE(pkt + 8);

	if (len != 8)
		return BSL_REP_INVALID_CMD;

	dl_status.start = addr;
	dl_status.size = size;
	dl_status.recv = 0;
	return BSL_REP_ACK;
}

static int data_midst(uint8_t *pkt) {
	unsigned len = READ16_BE(pkt + 2);
	unsigned remain = dl_status.size - dl_status.recv;
	uint32_t addr, fw_addr;
	uint8_t *src = pkt + 4;

	if (len > remain)
		return BSL_REP_DOWN_SIZE_ERROR;

	addr = dl_status.start + dl_status.recv;
	fw_addr = FW_ADDR;
	dl_status.recv += len;
	if ((addr - fw_addr) >> 24) {
		memcpy((uint8_t*)addr, src, len);
	} else {
#if WITH_SFC
#if ERASE_BLK > 0x1000
#error
#endif
		unsigned cs = SFC_BASE->cs & 1;
		uint8_t *buf = (uint8_t*)CHIPRAM_ADDR + 0x9000;
		unsigned blk = ERASE_BLK;
#if FDL_DEBUG
		uint8_t *src2 = (uint8_t*)CHIPRAM_ADDR + 0x4000 - len;
		memcpy(src2, src, len); src = src2;
#endif
		sfc_init();
		while (len) {
			unsigned i, n = blk - (addr & (blk - 1));
			unsigned diff = 0;
			if (n > len) n = len;
			len -= n;
			for (i = 0; i < n; i++) {
				int a = *(uint8_t*)(addr + i);
				int b = src[i];
				a ^= b;
				if (a & b) break;
				diff |= a;
			}
			if (i != n) {
				uint32_t addr2, tmp;
				memcpy(buf, (uint8_t*)(addr & -blk), addr & (blk - 1));
				addr2 = addr + n;
				memcpy(buf + (addr & (blk - 1)), src, n);
				tmp = -addr2 & (blk - 1);
				i = dl_status.start + dl_status.size - addr2;
				if (i > tmp) i = tmp;
				tmp = addr2 & (blk - 1); addr2 += i;
				memset(buf + tmp, -1, i);
				tmp = addr2 & (blk - 1);
				memcpy(buf + tmp, (uint8_t*)addr2, -addr2 & (blk - 1));
				addr2 = (addr & -blk) - fw_addr;
				DBG_LOG("sfc_erase %u, 0x%x\n", cs, addr2);
				sfc_erase(cs, addr2, ERASE_CMD, 3);
				DBG_LOG("sfc_write %u, 0x%x, %p, 0x%x\n", cs, addr2, buf, blk);
				sfc_write(cs, addr2, buf, blk);
			} else if (diff) {
				DBG_LOG("sfc_write %u, 0x%x, %p, 0x%x\n", cs, addr - fw_addr, src, n);
				sfc_write(cs, addr - fw_addr, src, n);
			}
			sfc_spiread(cs);
			addr += n; src += n;
		}
#else
		return BSL_REP_DOWN_SIZE_ERROR;
#endif
	}
	return BSL_REP_ACK;
}

static int data_end(void) {
	uint32_t recv = dl_status.recv;
	uint32_t size = dl_status.size;

	if (size != recv)
		return BSL_REP_DOWN_EARLY_END;
	return BSL_REP_ACK;
}

static int data_exec(void) {
	typedef void (*entry_t)(void);
	uintptr_t start = dl_status.start;

	if (start == -1)
		return BSL_REP_INVALID_CMD;

	((entry_t)start)();
	return BSL_REP_ACK;
}

static int set_baudrate(uint8_t *pkt) {
	unsigned len = READ16_BE(pkt + 2);
	uint32_t baudrate;

	if (len != 4)
		return BSL_REP_INVALID_CMD;

	baudrate = READ32_BE(pkt + 4);
	dl_channel->open(dl_channel, baudrate);
	return BSL_REP_ACK;
}

static int read_flash(uint8_t *pkt) {
	unsigned len = READ16_BE(pkt + 2);
	uint32_t addr = READ32_BE(pkt + 4);
	uint32_t size = READ32_BE(pkt + 8);
	uint32_t offs = -1;

	if ((len != 8 && len != 12) ||
			size >= MAX_PKT_PAYLOAD)
		return BSL_REP_INVALID_CMD;

	if (len == 12) offs = READ32_BE(pkt + 12);
	if (offs != -1) {
		if (addr == 0x80000003)
			addr = FW_ADDR + offs;
	}

	pkt = dl_send_buf();
	WRITE16_BE(pkt, BSL_REP_READ_FLASH);
	WRITE16_BE(pkt + 2, size);
	memcpy(pkt + 4, (void*)addr, size);
	dl_send_packet(pkt);
	return 0;
}

#if WITH_SFC
static int erase_flash(uint8_t *pkt) {
	unsigned len = READ16_BE(pkt + 2);
	uint32_t addr = READ32_BE(pkt + 4);
	uint32_t size = READ32_BE(pkt + 8);
	uint32_t fw_addr;
	unsigned i, blk = ERASE_BLK;

	if (len != 8)
		return BSL_REP_INVALID_CMD;

	fw_addr = FW_ADDR;
	if ((addr - fw_addr) >> 24 | ((addr | size) & (blk - 1)))
		return BSL_REP_INVALID_CMD;

	if (size) {
		unsigned cs = SFC_BASE->cs & 1;
		uint32_t end = addr + size;
		sfc_init();
		do {
			for (i = 0; i < blk; i++)
				if (~*(uint32_t*)(addr + i)) break;
			if (i != blk) {
				DBG_LOG("sfc_erase %u, 0x%x\n", cs, addr - fw_addr);
				sfc_erase(cs, addr - fw_addr, ERASE_CMD, 3);
				sfc_spiread(cs);
			}
		} while ((addr += blk) != end);
	}
	return BSL_REP_ACK;
}
#endif

void dl_main(void) {
	uint8_t *pkt;
#if 0
	static const char version[] = { "Spreadtrum Boot Block version 1.2" };
#else
	static char version[] = { "Custom FDL1: CHIP ID = 0x00000000" };
	{
		uint32_t i = 25, t, a;
		for (t = chip_id; t; t <<= 4) {
			a = t >> 28;
			if (a >= 10) a += 'a' - '0' - 10;
			version[i++] = a + '0';
		}
	}
#endif

	dl_packet_init();
	dl_status.start = -1;

	for (;;) {
		int ch = dl_channel->getchar(dl_channel, 1);
		if (ch == HDLC_HEADER) break;
	}

	pkt = dl_send_buf();
	WRITE16_BE(pkt, BSL_REP_VER);
	WRITE16_BE(pkt + 2, sizeof(version));
	memcpy(pkt + 4, version, sizeof(version));
	dl_send_packet(pkt);

	for (;;) {
		pkt = dl_get_packet();
		int type = READ16_BE(pkt);
		int ret;
		switch (type) {
		case BSL_CMD_CONNECT:
			ret = BSL_REP_ACK;
			break;

		case BSL_CMD_START_DATA:
			ret = data_start(pkt);
			break;

		case BSL_CMD_MIDST_DATA:
			ret = data_midst(pkt);
			break;

		case BSL_CMD_END_DATA:
			ret = data_end();
			break;

		case BSL_CMD_EXEC_DATA:
			ret = data_exec();
			break;

		case BSL_CMD_CHANGE_BAUD:
			ret = set_baudrate(pkt);
			break;

		case BSL_CMD_READ_FLASH:
			ret = read_flash(pkt);
			break;

#if WITH_SFC
		case BSL_CMD_ERASE_FLASH:
			ret = erase_flash(pkt);
			break;
#endif

		default:
			ret = BSL_REP_UNKNOWN_CMD;
		}
		if (ret > 0)
			dl_send_ack(ret);
	}
}

