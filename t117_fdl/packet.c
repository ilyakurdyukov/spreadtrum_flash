#include "common.h"
#include "packet.h"
#include "channel.h"

dl_channel_t *dl_channel;

uint8_t packet_temp[MAX_PKT_SIZE * 2 + 4];

void dl_packet_init(void) {
	dl_channel = dl_getchannel();
	dl_channel->open(dl_channel, -1);
}

static uint32_t dl_escapedata(uint8_t *dst_start, uint8_t *src, unsigned size) {
	uint16_t crc; uint8_t *dst;
	int i;

#if ACT_AS_ROMCODE
	crc = dl_crc16(src, size);
#else
	crc = dl_fastchk16((uint16_t*)src, size);
	crc = swap_be16(crc);
#endif

	WRITE16_BE(src + size, crc);
	size += 2;

	dst = dst_start;
	*dst++ = HDLC_HEADER;
	for (i = 0; i < size; i++) {
		int a = src[i];
		if (a == HDLC_HEADER || a == HDLC_ESCAPE) {
			*dst++ = HDLC_ESCAPE;
			a ^= 0x20;
		}
		*dst++ = a;
	}
	*dst++ = HDLC_HEADER;
	return dst - dst_start;
}

static void dl_write_packet(const void *buf, int len) {
	dl_channel->write(dl_channel, buf, len);
}

void dl_send_ack(int type) {
	unsigned len;
	uint8_t temp[16], *src = temp + 8;

	WRITE16_BE(src, type);
	WRITE16_BE(src + 2, 0);

	len = dl_escapedata(temp, src, 4);
	dl_write_packet(temp, len);
}

uint8_t* dl_get_packet(void) {
	uint8_t *data = packet_temp;
	int a;

	for (;;) {
		int head_found = 0, nread = 0, plen = 6, esc = 0;
		for (;;) {
			a = dl_channel->getchar(dl_channel, 1);
			if (esc && a != (HDLC_HEADER ^ 0x20) &&
					a != (HDLC_ESCAPE ^ 0x20)) break;
			if (a == HDLC_HEADER) {
				if (!head_found) head_found = 1;
				else if (!nread) continue;
				else if (nread < plen) break;
				else {
					unsigned chk;
#if ACT_AS_ROMCODE
					chk = dl_crc16(data, nread);
#else
					chk = dl_fastchk16((uint16_t*)data, nread);
#endif
					if (chk) break;
					/* success */
					return data;
				}
			} else if (a == HDLC_ESCAPE) {
				esc = 0x20;
			} else {
				if (!head_found) continue;
				if ((nread & ~1) >= plen) break;
				data[nread++] = a ^ esc;
				esc = 0;
			}
			if (nread == 4) {
				plen = READ16_BE(data + 2) + 6;
				if (plen > MAX_PKT_SIZE) break;
			}
		}
		/* error */
		dl_send_ack(BSL_REP_VERIFY_ERROR);
	}
}

void dl_send_packet(uint8_t *src) {
	uint8_t *temp = packet_temp;
	unsigned len = READ16_BE(src + 2);
	len = dl_escapedata(temp, src, len + 4);
	dl_write_packet(temp, len);
}


