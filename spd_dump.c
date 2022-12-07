/*
// Spreadtrum SC6531E/SC6531DA firmware dumper for Linux.
//
// sudo modprobe ftdi_sio
// echo 1782 4d00 | sudo tee /sys/bus/usb-serial/drivers/generic/new_id
// make && sudo ./spd_dump [options] commands...
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
*/

#define _GNU_SOURCE 1

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#ifndef LIBUSB_DETACH
/* detach the device from crappy kernel drivers */
#define LIBUSB_DETACH 1
#endif

#if USE_LIBUSB
#include <libusb-1.0/libusb.h>
#else
#include <termios.h>
#include <fcntl.h>
#include <poll.h>
#endif
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

static void print_string(FILE *f, const uint8_t *buf, size_t n) {
	size_t i; int a, b = 0;
	fprintf(f, "\"");
	for (i = 0; i < n; i++) {
		a = buf[i]; b = 0;
		switch (a) {
		case '"': case '\\': b = a; break;
		case 0: b = '0'; break;
		case '\b': b = 'b'; break;
		case '\t': b = 't'; break;
		case '\n': b = 'n'; break;
		case '\f': b = 'f'; break;
		case '\r': b = 'r'; break;
		}
		if (b) fprintf(f, "\\%c", b);
		else if (a >= 32 && a < 127) fprintf(f, "%c", a);
		else fprintf(f, "\\x%02x", a);
	}
	fprintf(f, "\"\n");
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define DBG_LOG(...) fprintf(stderr, __VA_ARGS__)

#define RECV_BUF_LEN 1024

typedef struct {
	uint8_t *raw_buf, *enc_buf, *recv_buf;
#if USE_LIBUSB
	libusb_device_handle *dev_handle;
	int endp_in, endp_out;
#else
	int serial;
#endif
	int flags, recv_len, recv_pos;
	int raw_len, enc_len, verbose, timeout;
} spdio_t;

#define FLAGS_CRC16 1
#define FLAGS_TRANSCODE 2

#if USE_LIBUSB
static void find_endpoints(libusb_device_handle *dev_handle, int result[2]) {
	int endp_in = -1, endp_out = -1;
	int i, k, err;
	//struct libusb_device_descriptor desc;
	struct libusb_config_descriptor *config;
	libusb_device *device = libusb_get_device(dev_handle);
	if (!device)
		ERR_EXIT("libusb_get_device failed\n");
	//if (libusb_get_device_descriptor(device, &desc) < 0)
	//	ERR_EXIT("libusb_get_device_descriptor failed");
	err = libusb_get_config_descriptor(device, 0, &config);
	if (err < 0)
		ERR_EXIT("libusb_get_config_descriptor failed : %s\n", libusb_error_name(err));

	for (k = 0; k < config->bNumInterfaces; k++) {
		const struct libusb_interface *interface;
		const struct libusb_interface_descriptor *interface_desc;
		int claim = 0;
		interface = config->interface + k;
		if (interface->num_altsetting < 1) continue;
		interface_desc = interface->altsetting + 0;
		for (i = 0; i < interface_desc->bNumEndpoints; i++) {
			const struct libusb_endpoint_descriptor *endpoint;
			endpoint = interface_desc->endpoint + i;
			if (endpoint->bmAttributes == 2) {
				int addr = endpoint->bEndpointAddress;
				err = 0;
				if (addr & 0x80) {
					if (endp_in >= 0) ERR_EXIT("more than one endp_in\n");
					endp_in = addr;
					claim = 1;
				} else {
					if (endp_out >= 0) ERR_EXIT("more than one endp_out\n");
					endp_out = addr;
					claim = 1;
				}
			}
		}
		if (claim) {
#if LIBUSB_DETACH
			err = libusb_kernel_driver_active(dev_handle, k);
			if (err > 0) {
				DBG_LOG("kernel driver is active, trying to detach\n");
				err = libusb_detach_kernel_driver(dev_handle, k);
				if (err < 0)
					ERR_EXIT("libusb_detach_kernel_driver failed : %s\n", libusb_error_name(err));
			}
#endif
			err = libusb_claim_interface(dev_handle, k);
			if (err < 0)
				ERR_EXIT("libusb_claim_interface failed : %s\n", libusb_error_name(err));
			break;
		}
	}
	if (endp_in < 0) ERR_EXIT("endp_in not found\n");
	if (endp_out < 0) ERR_EXIT("endp_out not found\n");
	libusb_free_config_descriptor(config);

	//DBG_LOG("USB endp_in=%02x, endp_out=%02x\n", endp_in, endp_out);

	result[0] = endp_in;
	result[1] = endp_out;
}
#else
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
#endif

#if USE_LIBUSB
static spdio_t* spdio_init(libusb_device_handle *dev_handle, int flags) {
#else
static spdio_t* spdio_init(int serial, int flags) {
#endif
	uint8_t *p; spdio_t *io;

#if USE_LIBUSB
	int endpoints[2];
	find_endpoints(dev_handle, endpoints);
#else
	init_serial(serial);
	// fcntl(serial, F_SETFL, FNDELAY);
	tcflush(serial, TCIOFLUSH);
#endif

	p = (uint8_t*)malloc(sizeof(spdio_t) + RECV_BUF_LEN + (4 + 0x10000 + 2) * 3 + 2);
	io = (spdio_t*)p; p += sizeof(spdio_t);
	if (!p) ERR_EXIT("malloc failed\n");
	io->flags = flags;
#if USE_LIBUSB
	io->dev_handle = dev_handle;
	io->endp_in = endpoints[0];
	io->endp_out = endpoints[1];
#else
	io->serial = serial;
#endif
	io->recv_len = 0;
	io->recv_pos = 0;
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->raw_buf = p; p += 4 + 0x10000 + 2;
	io->enc_buf = p;
	io->verbose = 0;
	io->timeout = 1000;
	return io;
}

static void spdio_free(spdio_t* io) {
	if (!io) return;
#if USE_LIBUSB
	libusb_close(io->dev_handle);
#else
	close(io->serial);
#endif
	free(io);
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

#define CHK_FIXZERO 1
#define CHK_ORIG 2

static unsigned spd_checksum(unsigned crc, const void *src, int len, int final) {
	uint8_t *s = (uint8_t*)src;

	while (len > 1) {
		crc += s[1] << 8 | s[0]; s += 2;
		len -= 2;
	}
	if (len) crc += *s;
	if (final) {
		crc = (crc >> 16) + (crc & 0xffff);
		crc += crc >> 16;
		crc = ~crc & 0xffff;
		if (len < final)
			crc = crc >> 8 | (crc & 0xff) << 8;
	}
	return crc;
}

#define WRITE16_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 8; \
	((uint8_t*)(p))[1] = (uint8_t)(a); \
} while (0)

#define WRITE32_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 24; \
	((uint8_t*)(p))[1] = (a) >> 16; \
	((uint8_t*)(p))[2] = (a) >> 8; \
	((uint8_t*)(p))[3] = (uint8_t)(a); \
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

	len = p - p0;
	if (io->flags & FLAGS_CRC16)
		chk = spd_crc16(0, p0, len);
	else {
		// if (len & 1) *p++ = 0;
		chk = spd_checksum(0, p0, len, CHK_FIXZERO);
	}
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

#if USE_LIBUSB
	{
		int err = libusb_bulk_transfer(io->dev_handle,
				io->endp_out, io->enc_buf, io->enc_len, &ret, io->timeout);
		if (err < 0)
			ERR_EXIT("usb_send failed : %s\n", libusb_error_name(err));
	}
#else
	ret = write(io->serial, io->enc_buf, io->enc_len);
#endif
	if (ret != io->enc_len)
		ERR_EXIT("usb_send failed (%d / %d)\n", ret, io->enc_len);

#if !USE_LIBUSB
	tcdrain(io->serial);
	// usleep(1000);
#endif
	return ret;
}

static int recv_msg(spdio_t *io) {
	int a, pos, len, chk;
	int esc = 0, nread = 0, head_found = 0, plen = 6;

	len = io->recv_len;
	pos = io->recv_pos;
	for (;;) {
		if (pos >= len) {
#if USE_LIBUSB
			int err = libusb_bulk_transfer(io->dev_handle, io->endp_in, io->recv_buf, RECV_BUF_LEN, &len, io->timeout);
			if (err == LIBUSB_ERROR_NO_DEVICE)
				ERR_EXIT("connection closed\n");
			else if (err == LIBUSB_ERROR_TIMEOUT) break;
			else if (err < 0)
				ERR_EXIT("usb_recv failed : %s\n", libusb_error_name(err));
#else
			if (io->timeout >= 0) {
				struct pollfd fds = { 0 };
				fds.fd = io->serial;
				fds.events = POLLIN;
				a = poll(&fds, 1, io->timeout);
				if (a < 0) ERR_EXIT("poll failed, ret = %d\n", a);
				if (fds.revents & POLLHUP)
					ERR_EXIT("connection closed\n");
				if (!a) break;
			}
			len = read(io->serial, io->recv_buf, RECV_BUF_LEN);
#endif
			if (len < 0)
				ERR_EXIT("usb_recv failed, ret = %d\n", len);

			if (io->verbose >= 2) {
				DBG_LOG("recv (%d):\n", len);
				print_mem(stderr, io->recv_buf, len);
			}
			pos = 0;
			if (!len) break;
		}
		a = io->recv_buf[pos++];
		if (io->flags & FLAGS_TRANSCODE) {
			if (esc && a != (HDLC_HEADER ^ 0x20) &&
					a != (HDLC_ESCAPE ^ 0x20))
				ERR_EXIT("unexpected escaped byte (0x%02x)\n", a);
			if (a == HDLC_HEADER) {
				if (!head_found) head_found = 1;
				else if (!nread) continue;
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
		chk = spd_checksum(0, io->raw_buf, plen - 2, CHK_ORIG);

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
	free(mem);

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
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			DBG_LOG("unexpected response (0x%04x)\n", ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread) 
			ERR_EXIT("fwrite(dump) failed\n");
		off += nread;
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
		if ((ret = recv_type(io)) != BSL_REP_READ_FLASH) {
			DBG_LOG("unexpected response (0x%04x)\n", ret);
			break;
		}
		nread = READ16_BE(io->raw_buf + 2);
		if (n < nread)
			ERR_EXIT("unexpected length\n");
		if (fwrite(io->raw_buf + 4, 1, nread, fo) != nread) 
			ERR_EXIT("fwrite(dump) failed\n");
		off += nread;
		if (n != nread) break;
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", start, len, off - start);
	fclose(fo);
	return off;
}

#define REOPEN_FREQ 2

int main(int argc, char **argv) {
#if USE_LIBUSB
	libusb_device_handle *device;
#else
	int serial;
#endif
	spdio_t *io; int ret, i;
	int wait = 30 * REOPEN_FREQ;
	const char *tty = "/dev/ttyUSB0";
	int verbose = 0, fdl_loaded = 0;

#if USE_LIBUSB
	ret = libusb_init(NULL);
	if (ret < 0)
		ERR_EXIT("libusb_init failed: %s\n", libusb_error_name(ret));
#endif

	while (argc > 1) {
		if (!strcmp(argv[1], "--tty")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			tty = argv[2];
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--wait")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			wait = atoi(argv[2]) * REOPEN_FREQ;
			argc -= 2; argv += 2;
		} else if (!strcmp(argv[1], "--verbose")) {
			if (argc <= 2) ERR_EXIT("bad option\n");
			verbose = atoi(argv[2]);
			argc -= 2; argv += 2;
		} else if (argv[1][0] == '-') {
			ERR_EXIT("unknown option\n");
		} else break;
	}

	for (i = 0; ; i++) {
#if USE_LIBUSB
		device = libusb_open_device_with_vid_pid(NULL, 0x1782, 0x4d00);
		if (device) break;
		if (i >= wait)
			ERR_EXIT("libusb_open_device failed\n");
#else
		serial = open(tty, O_RDWR | O_NOCTTY | O_SYNC);
		if (serial >= 0) break;
		if (i >= wait)
			ERR_EXIT("open(ttyUSB) failed\n");
#endif
		if (!i) DBG_LOG("Waiting for connection (%ds)\n", wait / REOPEN_FREQ);
		usleep(1000000 / REOPEN_FREQ);
	}

#if USE_LIBUSB
	io = spdio_init(device, 0);
#else
	io = spdio_init(serial, 0);
#endif
	io->flags |= FLAGS_TRANSCODE;
	io->verbose = verbose;

	while (argc > 1) {
		if (!strcmp(argv[1], "fdl")) {
			const char *fn; uint32_t addr;
			if (argc <= 3) ERR_EXIT("bad command\n");

			fn = argv[2];
			addr = strtol(argv[3], NULL, 0);

			if (!fdl_loaded) {
				/* Bootloader (chk = crc16) */
				io->flags |= FLAGS_CRC16;

				encode_msg(io, BSL_CMD_CHECK_BAUD, NULL, 1);
				send_msg(io);
				ret = recv_msg(io);
				if (recv_type(io) != BSL_REP_VER)
					ERR_EXIT("ver expected\n");

				DBG_LOG("BSL_REP_VER: ");
				print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));

				encode_msg(io, BSL_CMD_CONNECT, NULL, 0);
				send_msg(io);
				ret = recv_msg(io);
				if (recv_type(io) != BSL_REP_ACK)
					ERR_EXIT("ack expected\n");

				send_file(io, fn, addr);

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

				DBG_LOG("BSL_REP_VER: ");
				print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));

				encode_msg(io, BSL_CMD_CONNECT, NULL, 0);
				send_msg(io);
				ret = recv_msg(io);
				if (recv_type(io) != BSL_REP_ACK)
					ERR_EXIT("ack expected\n");

			} else {

				send_file(io, fn, addr);

				encode_msg(io, BSL_CMD_EXEC_DATA, NULL, 0);
				send_msg(io);
				ret = recv_msg(io);
				if (recv_type(io) != BSL_REP_ACK)
					ERR_EXIT("ack expected\n");
			}

			fdl_loaded++;
			argc -= 3; argv += 3;
		} else if (!strcmp(argv[1], "read_flash")) {
			const char *fn; uint32_t addr, offset, size;
			if (argc <= 5) ERR_EXIT("bad command\n");

			addr = strtol(argv[2], NULL, 0);
			offset = strtol(argv[3], NULL, 0);
			size = strtol(argv[4], NULL, 0);
			fn = argv[5];
			dump_flash(io, addr, offset, size, fn);
			argc -= 5; argv += 5;
		} else if (!strcmp(argv[1], "read_mem")) {
			const char *fn; uint32_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = strtol(argv[2], NULL, 0);
			size = strtol(argv[3], NULL, 0);
			fn = argv[4];
			dump_mem(io, addr, size, fn);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "verbose")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->verbose = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else {
			ERR_EXIT("unknown command\n");
		}
	}

	spdio_free(io);
#if USE_LIBUSB
	libusb_exit(NULL);
#endif
	return 0;
}
