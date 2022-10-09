/*
// Spreadtrum SC6531E feature phone firmware dumper for Linux.
//
// sudo modprobe ftdi_sio
// echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
// make && sudo ./spd_dump
*/

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <termios.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include "spd_cmd.h"

static void print_mem(FILE *f, uint8_t *buf, size_t len) {
	size_t i; int a, j, n;
	for (i = 0; i < len; i += 16) {
		n = len - i;
		if (n > 16) n = 16;
		for (j = 0; j < n; j++) fprintf(f, "%02x ", buf[i + j]);
		for (; j < 16; j++) fprintf(f, "   ");
		fprintf(f, " |");
		for (j = 0; j < n; j++) {
			a = buf[i + j];
			fprintf(f, "%c", a > 0x20 && a < 0x7f ? a : '.');
		}
		fprintf(f, "|\n");
	}
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define RECV_BUF_LEN 1024

typedef struct {
	uint8_t *raw_buf, *enc_buf, *recv_buf;
	int flags, serial, recv_len, recv_pos;
	int raw_len, enc_len, verbose, timeout;
} spdio_t;

#define FLAGS_CRC16 1
#define FLAGS_TRANSCODE 2

static spdio_t* spdio_init(int serial, int flags) {
	uint8_t *p; spdio_t *io;

	p = (uint8_t*)malloc(sizeof(spdio_t) + RECV_BUF_LEN + (4 + 0xffff + 2) * 3 + 2);
	io = (spdio_t*)p; p += sizeof(spdio_t);
	if (!p) ERR_EXIT("malloc failed\n");
	io->flags = flags;
	io->serial = serial;
	io->recv_len = 0;
	io->recv_pos = 0;
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->raw_buf = p; p += 4 + 0xffff + 2;
	io->enc_buf = p;
	io->verbose = 0;
	io->timeout = 1000;
	return io;
}

static void spdio_free(spdio_t* io) {
	if (io) free(io);
}

static void init_serial(int serial) {
	struct termios tty = { 0 };

	// B921600
	cfsetispeed(&tty, B115200);
	cfsetospeed(&tty, B115200);

	tty.c_cflag = CS8 | CLOCAL | CREAD;
	tty.c_iflag = IGNPAR;
	tty.c_oflag = 0;
	tty.c_lflag = 0;

	tty.c_cc[VMIN] = 1;
	tty.c_cc[VTIME] = 0;

	tcflush(serial, TCIFLUSH);
	tcsetattr(serial, TCSANOW, &tty);
}

static int spd_transcode(uint8_t *dst, uint8_t *src, int len) {
	int i, a, n = 0;
	for (i = 0; i < len; i++) {
		a = src[i];
		if (a == HDLC_HEADER || a == HDLC_ESCAPE) {
			if (dst) dst[n] = HDLC_ESCAPE;
			n++;
			a ^= 0x20;
		}
		if (dst) dst[n] = a;
		n++;
	}
	return n;
}

static int spd_transcode_max(uint8_t *src, int len, int n) {
	int i, a;
	for (i = 0; i < len; i++) {
		a = src[i];
		a = a == HDLC_HEADER || a == HDLC_ESCAPE ? 2 : 1;
		if (n < a) break;
		n -= a;
	}
	return i;
}

static unsigned spd_crc16(unsigned crc, const void *src, unsigned len) {
	uint8_t *s = (uint8_t*)src; int i;
	crc &= 0xffff;
	while (len--) {
		crc ^= *s++ << 8;
		for (i = 0; i < 8; i++)
			crc = crc << 1 ^ ((0 - (crc >> 15)) & 0x11021);
	}
	return crc;
}

static unsigned spd_checksum(unsigned crc, const void *src, int len, int final) {
	uint8_t *s = (uint8_t*)src;

	while (len > 1) {
		crc += s[0] << 8 | s[1]; s += 2;
		len -= 2;
	}
	if (len == 1)	crc += *s;
	if (final) {
		crc = (crc >> 16) + (crc & 0xffff);
		crc += crc >> 16;
		crc = ~crc & 0xffff;
	}
	return crc;
}

#define WRITE16_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 8; \
	((uint8_t*)(p))[1] = (a); \
} while (0)

#define WRITE32_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 24; \
	((uint8_t*)(p))[1] = (a) >> 16; \
	((uint8_t*)(p))[2] = (a) >> 8; \
	((uint8_t*)(p))[3] = (a); \
} while (0)

#define READ16_BE(p) ( \
	((uint8_t*)(p))[0] << 8 | \
	((uint8_t*)(p))[1])

#define READ32_BE(p) ( \
	((uint8_t*)(p))[0] << 24 | \
	((uint8_t*)(p))[1] << 16 | \
	((uint8_t*)(p))[2] << 8 | \
	((uint8_t*)(p))[3])

static void encode_msg(spdio_t *io, int type, const void *data, size_t len) {
	uint8_t *p, *p0; unsigned chk;
	int i;

	if (len > 0xffff)
		ERR_EXIT("message too long\n");

	if (type == BSL_CMD_CHECK_BAUD) {
		memset(io->enc_buf, HDLC_HEADER, len);
		io->enc_len = len;
		return;
	}

	p = p0 = io->raw_buf;
	WRITE16_BE(p, type); p += 2;
	WRITE16_BE(p, len); p += 2;
	memcpy(p, data, len); p += len;
	if (len & 1) *p++ = 0;

	len = p - p0;
	if (io->flags & FLAGS_CRC16)
		chk = spd_crc16(0, p0, len);
	else chk = spd_checksum(0, p0, len, 1);
	WRITE16_BE(p, chk); p += 2;

	io->raw_len = len = p - p0;

	p = io->enc_buf;
	*p++ = HDLC_HEADER;
	if (io->flags & FLAGS_TRANSCODE)
		len = spd_transcode(p, p0, len);
	else memcpy(p, p0, len);
	p[len] = HDLC_HEADER;
	io->enc_len = len + 2;
}

static int send_msg(spdio_t *io) {
	int ret;
	if (!io->enc_len)
		ERR_EXIT("empty message\n");

	if (io->verbose >= 2) {
		DBG_LOG("send (%d):\n", io->enc_len);
		print_mem(stderr, io->enc_buf, io->enc_len);
	} else if (io->verbose >= 1) {
		if (io->raw_buf[0] == HDLC_HEADER)
			DBG_LOG("send: check baud\n");
		else if (io->raw_len >= 4) {
			DBG_LOG("send: type = 0x%02x, size = %d\n",
					READ16_BE(io->raw_buf), READ16_BE(io->raw_buf + 2));
		} else DBG_LOG("send: unknown message\n");
	}

	ret = write(io->serial, io->enc_buf, io->enc_len);
	if (ret != io->enc_len)
		ERR_EXIT("write(message) failed\n");

	tcdrain(io->serial);
	// usleep(1000);
	return ret;
}

static int recv_msg(spdio_t *io) {
	int a, pos, len, chk;
	int esc = 0, nread = 0, head_found = 0, plen = 6;

	len = io->recv_len;
	pos = io->recv_pos;
	for (;;) {
		if (pos >= len) {
			if (io->timeout >= 0) {
				struct pollfd fds = { 0 };
				fds.fd = io->serial;
				fds.events = POLLIN;
				a = poll(&fds, 1, io->timeout);
				if (a < 0) ERR_EXIT("poll failed, ret = %d\n", a);
				if (!a) break;
			}
			pos = 0;
			len = read(io->serial, io->recv_buf, RECV_BUF_LEN);
			if (len < 0)
				ERR_EXIT("read(message) failed, ret = %d\n", len);

			if (io->verbose >= 2) {
				DBG_LOG("recv (%d):\n", len);
				print_mem(stderr, io->recv_buf, len);
			}
			if (!len) break;
		}
		a = io->recv_buf[pos++];
		if (io->flags & FLAGS_TRANSCODE) {
			if (a == HDLC_HEADER) {
				esc = 0;
				if (!head_found) head_found = 1;
				else if (nread < plen)
					ERR_EXIT("recieved message too short\n");
				else break;
			} else if (a == HDLC_ESCAPE) {
				esc = 0x20;
			} else {
				if (!head_found) continue;
				if (nread >= plen)
					ERR_EXIT("recieved message too long\n");
				io->raw_buf[nread++] = a ^ esc;
				esc = 0;
			}
		} else {
			if (!head_found && a == HDLC_HEADER) {
				head_found = 1;
				continue;
			}
			if (nread == plen) {
				if (a != HDLC_HEADER)
					ERR_EXIT("expected end of message\n");
				break;
			}
			io->raw_buf[nread++] = a;
		}
		if (nread == 4) {
			a = READ16_BE(io->raw_buf + 2);	// len
			plen = a + 6;
		}
	}
	io->recv_len = len;
	io->recv_pos = pos;
	io->raw_len = nread;
	if (!nread) return 0;

	if (nread < 6)
		ERR_EXIT("recieved message too short\n");

	if (nread != plen)
		ERR_EXIT("bad length (%d, expected %d)\n", nread, plen);

	if (io->flags & FLAGS_CRC16)
		chk = spd_crc16(0, io->raw_buf, plen - 2);
	else
		chk = spd_checksum(0, io->raw_buf, plen - 2, 1);

	a = READ16_BE(io->raw_buf + plen - 2);
	if (a != chk)
		ERR_EXIT("bad checksum (0x%04x, expected 0x%04x)\n", a, chk);

	if (io->verbose == 1)
		DBG_LOG("recv: type = 0x%02x, size = %d\n",
				READ16_BE(io->raw_buf), READ16_BE(io->raw_buf + 2));

	return nread;
}

static unsigned recv_type(spdio_t *io) {
	int a;
	if (io->raw_len < 6) return -1;
	return READ16_BE(io->raw_buf);
}

static uint8_t* loadfile(const char *fn, size_t *num) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		if (n) {
			fseek(fi, 0, SEEK_SET);
			buf = (uint8_t*)malloc(n);
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

static void send_file(spdio_t *io, const char *fn, uint32_t start_addr) {
	uint8_t *mem; size_t size = 0;
	uint32_t data[2], i, n, step = 1024;
	int ret;
	mem = loadfile(fn, &size);
	if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
	if (size >> 32) ERR_EXIT("file too big\n");

	WRITE32_BE(data, start_addr);
	WRITE32_BE(data + 1, size);

	encode_msg(io, BSL_CMD_START_DATA, data, 4 * 2);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");

	for (i = 0; i < size; i += n) {
		n = size - i;
		// n = spd_transcode_max(mem + i, size - i, 2048 - 2 - 6);
		if (n > step) n = step;
		encode_msg(io, BSL_CMD_MIDST_DATA, mem + i, n);
		send_msg(io);
		ret = recv_msg(io);
		if (recv_type(io) != BSL_REP_ACK)
			ERR_EXIT("ack expected\n");
	}

	encode_msg(io, BSL_CMD_END_DATA, NULL, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");
}

static unsigned dump_flash(spdio_t *io,
		uint32_t addr, uint32_t start, uint32_t len, const char *fn) {
	uint32_t n, off, nread, step = 1024;
	int ret;
	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (off = start; off < start + len; ) {
		uint32_t data[3];
		n = start + len - off;
		if (n > step) n = step;

		WRITE32_BE(data, addr);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, off);

		encode_msg(io, BSL_CMD_READ_FLASH, data, 4 * 3);
		send_msg(io);
		ret = recv_msg(io);
		if (recv_type(io) != BSL_REP_READ_FLASH) {
			DBG_LOG("unexpected response\n");
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread) 
			ERR_EXIT("fwrite(dump) failed\n");
		off += n;
		if (n != nread) break;
	}
	DBG_LOG("dump_flash: 0x%08x+0x%x, target: 0x%x, read: 0x%x\n", addr, start, len, off - start);
	fclose(fo);
	return off;
}

static unsigned dump_mem(spdio_t *io,
		uint32_t start, uint32_t len, const char *fn) {
	uint32_t n, off, nread, step = 1024;
	int ret;
	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (off = start; off < start + len; ) {
		uint32_t data[3];
		n = start + len - off;
		if (n > step) n = step;

		WRITE32_BE(data, off);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, 0);	// unused

		encode_msg(io, BSL_CMD_READ_FLASH, data, 4 * 3);
		send_msg(io);
		ret = recv_msg(io);
		if (recv_type(io) != BSL_REP_READ_FLASH) {
			DBG_LOG("unexpected response\n");
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread) 
			ERR_EXIT("fwrite(dump) failed\n");
		off += n;
		if (n != nread) break;
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", start, len, off - start);
	fclose(fo);
	return off;
}

int main(int argc, char **argv) {
	int serial; spdio_t *io; int ret, i;

	serial = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY | O_SYNC);
	if (serial < 0)
		ERR_EXIT("open(ttyUSB) failed\n");

	init_serial(serial);
	// fcntl(serial, F_SETFL, FNDELAY);
	tcflush(serial, TCIOFLUSH);

	io = spdio_init(serial, FLAGS_CRC16 | FLAGS_TRANSCODE);

	io->verbose = 2;

	/* Bootloader (chk = crc16) */

	encode_msg(io, BSL_CMD_CHECK_BAUD, NULL, 4);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_VER)
		ERR_EXIT("ver expected\n");

	encode_msg(io, BSL_CMD_CONNECT, NULL, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");

	send_file(io, "nor_fdl1.bin", 0x40004000);

	encode_msg(io, BSL_CMD_EXEC_DATA, NULL, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");

	/* FDL1 (chk = sum) */

	io->flags &= ~FLAGS_CRC16;
	encode_msg(io, BSL_CMD_CHECK_BAUD, NULL, 4);
	for (i = 0; i < 10; i++) {
		if (io->verbose >= 1)
			DBG_LOG("checkbaud %d\n", i + 1);
		send_msg(io);
		ret = recv_msg(io);
		if (ret) break;
	}
	if (recv_type(io) != BSL_REP_VER)
		ERR_EXIT("ver expected\n");

	encode_msg(io, BSL_CMD_CONNECT, NULL, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");

	send_file(io, "nor_fdl.bin", 0x14000000);

	encode_msg(io, BSL_CMD_EXEC_DATA, NULL, 0);
	send_msg(io);
	ret = recv_msg(io);
	if (recv_type(io) != BSL_REP_ACK)
		ERR_EXIT("ack expected\n");

	/* FDL2 (chk = sum) */

	dump_flash(io, 0x80000003, 0, 4 << 20, "dump.bin");

	// 03c00000 - these bits are ignored
	//dump_mem(io, 0x10000000, 4 << 20, "dump.bin"); // same as dump_flash(0x80000003)
	//dump_mem(io, 0x14000000, 4 << 20, "extra.bin"); // FDL2 and some memory

	spdio_free(io);
	close(serial);
	return 0;
}
