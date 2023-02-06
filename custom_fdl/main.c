#include "common.h"
#include "cmd_def.h"
#include "packet.h"
#include "channel.h"

static struct {
	uintptr_t start;
	uint32_t size, recv;
} dl_status;

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
	if (len > remain)
		return BSL_REP_DOWN_SIZE_ERROR;
	memcpy((uint8_t*)dl_status.start + dl_status.recv, pkt + 4, len);
	dl_status.recv += len;
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
			addr = (_chip != 1 ? 0x30000000 : 0x10000000) + offs;
	}

	pkt = dl_send_buf();
	WRITE16_BE(pkt, BSL_REP_READ_FLASH);
	WRITE16_BE(pkt + 2, size);
	memcpy(pkt + 4, (void*)addr, size);
	dl_send_packet(pkt);
	return 0;
}

void dl_main(void) {
	uint8_t *pkt;
#if 0
	static const char version[] = { "Spreadtrum Boot Block version 1.2" };
#else
	static char version[] = { "Custom FDL1: CHIP ID = 0x00000000" };
	{
		uint32_t i = 25, t;
		for (t = chip_id; t; t <<= 4)
			version[i++] = "0123456789abcdef"[t >> 28];
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

		default:
			ret = BSL_REP_UNKNOWN_CMD;
		}
		if (ret > 0)
			dl_send_ack(ret);
	}
}

