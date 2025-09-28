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
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h> // tolower

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

static void print_string(FILE *f, const void *src, size_t n) {
	size_t i; int a, b = 0;
	const uint8_t *buf = (const uint8_t*)src;
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
	uint8_t *raw_buf, *enc_buf, *recv_buf, *temp_buf;
#if USE_LIBUSB
	libusb_device_handle *dev_handle;
	int endp_in, endp_out;
	int endp_in_blk, endp_out_blk;
#else
	int serial;
#endif
	int flags, recv_len, recv_pos;
	int raw_len, enc_len, verbose, timeout;
} spdio_t;

#define FLAGS_CRC16 1
#define FLAGS_TRANSCODE 2

#if USE_LIBUSB
static void find_endpoints(libusb_device_handle *dev_handle, int result[4]) {
	int endp_in = -1, endp_out = -1, endp_in_blk = 0, endp_out_blk = 0;
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
					endp_in_blk = endpoint->wMaxPacketSize;
					claim = 1;
				} else {
					if (endp_out >= 0) ERR_EXIT("more than one endp_out\n");
					endp_out = addr;
					endp_out_blk = endpoint->wMaxPacketSize;
					claim = 1;
				}
			}
		}
		if (claim) {
			i = interface_desc->bInterfaceNumber;
#if LIBUSB_DETACH
			err = libusb_kernel_driver_active(dev_handle, i);
			if (err > 0) {
				DBG_LOG("kernel driver is active, trying to detach\n");
				err = libusb_detach_kernel_driver(dev_handle, i);
				if (err < 0)
					ERR_EXIT("libusb_detach_kernel_driver failed : %s\n", libusb_error_name(err));
			}
#endif
			err = libusb_claim_interface(dev_handle, i);
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
	result[2] = endp_in_blk;
	result[3] = endp_out_blk;
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
	int endpoints[4];
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
	io->endp_in_blk = endpoints[2];
	io->endp_out_blk = endpoints[3];
#else
	io->serial = serial;
#endif
	io->recv_len = 0;
	io->recv_pos = 0;
	io->recv_buf = p; p += RECV_BUF_LEN;
	io->temp_buf = p + 4;
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

__attribute__((unused))
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

#define WRITE16_LE(p, a) do { \
	((uint8_t*)(p))[0] = (uint8_t)(a); \
	((uint8_t*)(p))[1] = (a) >> 8; \
} while (0)

#define WRITE32_LE(p, a) do { \
	((uint8_t*)(p))[0] = (uint8_t)(a); \
	((uint8_t*)(p))[1] = (a) >> 8; \
	((uint8_t*)(p))[2] = (a) >> 16; \
	((uint8_t*)(p))[3] = (a) >> 24; \
} while (0)

#define READ32_LE(p) ( \
	((uint8_t*)(p))[0] | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[3] << 24)

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
	// UMS9117 waits too long after a 512 byte block.
	if (io->endp_out_blk == 512 && !((unsigned)io->enc_len % 512)) {
		int dummy;
		// signal end of transfer
		libusb_bulk_transfer(io->dev_handle,
				io->endp_out, NULL, 0, &dummy, io->timeout);
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

static int recv_msg1(spdio_t *io) {
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
	if (io->raw_len < 6) return -1;
	return READ16_BE(io->raw_buf);
}

static int recv_msg(spdio_t *io) {
	int ret;
	for (;;) {
		ret = recv_msg1(io);
		if (!ret || recv_type(io) != BSL_REP_LOG) break;
		DBG_LOG("BSL_REP_LOG: ");
		print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
	}
	return ret;
}

static int recv_msg_timeout(spdio_t *io, int timeout) {
	int old = io->timeout, ret;
	io->timeout = old > timeout ? old : timeout;
	ret = recv_msg(io);
	io->timeout = old;
	return ret;
}

static void send_and_check(spdio_t *io) {
	int ret;
	send_msg(io);
	ret = recv_msg(io);
	if (!ret) ERR_EXIT("timeout reached\n");
	ret = recv_type(io);
	if (ret != BSL_REP_ACK)
		ERR_EXIT("unexpected response (0x%04x)\n", ret);
}

static void check_confirm(const char *name) {
	char buf[4], c; int i;
	printf("Answer \"yes\" to confirm the \"%s\" command: ", name);
	fflush(stdout);
	do {
		i = scanf("%3s%c", buf, &c);
		if (i != 2 || c != '\n') break;
		for (i = 0; buf[i]; i++) buf[i] = tolower(buf[i]);
		if (!strcmp(buf, "yes")) return;
	} while (0);
	ERR_EXIT("operation is not confirmed\n");
}

static uint8_t* loadfile(const char *fn, size_t *num, size_t extra) {
	size_t n, j = 0; uint8_t *buf = 0;
	FILE *fi = fopen(fn, "rb");
	if (fi) {
		fseek(fi, 0, SEEK_END);
		n = ftell(fi);
		if (n) {
			fseek(fi, 0, SEEK_SET);
			buf = (uint8_t*)malloc(n + extra);
			if (buf) j = fread(buf, 1, n, fi);
		}
		fclose(fi);
	}
	if (num) *num = j;
	return buf;
}

static void send_buf(spdio_t *io,
		uint32_t start_addr, int end_data,
		unsigned step, uint8_t *mem, unsigned size) {
	uint32_t data[2], i, n;

	WRITE32_BE(data, start_addr);
	WRITE32_BE(data + 1, size);

	encode_msg(io, BSL_CMD_START_DATA, data, 4 * 2);
	send_and_check(io);
	for (i = 0; i < size; i += n) {
		n = size - i;
		// n = spd_transcode_max(mem + i, size - i, 2048 - 2 - 6);
		if (n > step) n = step;
		encode_msg(io, BSL_CMD_MIDST_DATA, mem + i, n);
		send_and_check(io);
	}
	if (!end_data) return;
	encode_msg(io, BSL_CMD_END_DATA, NULL, 0);
	send_and_check(io);
}

static void send_file(spdio_t *io, const char *fn,
		uint32_t start_addr, int end_data, unsigned step,
		unsigned src_offs, unsigned src_size) {
	uint8_t *mem; size_t size = 0;
	mem = loadfile(fn, &size, 0);
	if (!mem) ERR_EXIT("loadfile(\"%s\") failed\n", fn);
	if (size >> 32) ERR_EXIT("file too big\n");
	if (size < src_offs)
		ERR_EXIT("data outside the file\n");
	size -= src_offs;
	if (src_size) {
		if (size < src_size)
			ERR_EXIT("data outside the file\n");
		size = src_size;
	}
	send_buf(io, start_addr, end_data, step, mem + src_offs, size);
	free(mem);
}

static void erase_flash(spdio_t *io, uint32_t addr, uint32_t size) {
	uint32_t data[2];
	check_confirm("erase flash");
	WRITE32_BE(data, addr);
	WRITE32_BE(data + 1, size);
	encode_msg(io, BSL_CMD_ERASE_FLASH, data, 4 * 2);
	send_and_check(io);
}

static unsigned dump_flash(spdio_t *io,
		uint32_t addr, uint32_t start, uint32_t len,
		const char *fn, unsigned step, int mode) {
	uint32_t n, offset, nread;
	int ret;
	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	if (mode == 1) {
		len = 0x200;
		if (step < 64) step = 64;
	}

	for (offset = start; offset < start + len; ) {
		uint32_t data[3];
		n = start + len - offset;
		if (n > step) n = step;
		WRITE32_BE(data, addr);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, offset);

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
		if (!offset && mode == 1) {
			uint8_t *p = io->raw_buf + 4;
			if (nread < 0x34) ERR_EXIT("can't read DHTB header\n");
			// "DHTB" -> "BTHD", Boot Header
			if (READ32_LE(p) != 0x42544844 || READ32_LE(p + 4) != 1)
				ERR_EXIT("unexpected DHTB header\n");
			len = READ32_LE(p + 0x30);
			if (len >> 31) ERR_EXIT("unexpected DHTB size (0x%x)\n", len);
			len += 0x200;
		}
		offset += nread;
		if (n != nread) break;
	}
	DBG_LOG("dump_flash: 0x%08x+0x%x, target: 0x%x, read: 0x%x\n", addr, start, len, offset - start);
	fclose(fo);
	return offset;
}

static unsigned dump_mem(spdio_t *io,
		uint32_t start, uint32_t len, const char *fn, unsigned step) {
	uint32_t n, offset, nread;
	int ret;
	FILE *fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (offset = start; offset < start + len; ) {
		uint32_t data[3];
		n = start + len - offset;
		if (n > step) n = step;

		WRITE32_BE(data, offset);
		WRITE32_BE(data + 1, n);
		WRITE32_BE(data + 2, 0);	// unused

		encode_msg(io, BSL_CMD_READ_FLASH, data, sizeof(data));
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
		offset += nread;
		if (n != nread) break;
	}
	DBG_LOG("dump_mem: 0x%08x, target: 0x%x, read: 0x%x\n", start, len, offset - start);
	fclose(fo);
	return offset;
}

static int copy_to_wstr(uint16_t *d, size_t n, const char *s) {
	size_t i; int a = -1;
	for (i = 0; a && i < n; i++) { a = s[i]; WRITE16_LE(d + i, a); }
	return a;
}

static int copy_from_wstr(char *d, size_t n, const uint16_t *s) {
	size_t i; int a = -1;
	for (i = 0; a && i < n; i++) { d[i] = a = s[i]; if (a >> 8) break; }
	return a;
}

static void select_partition(spdio_t *io, const char *name,
		uint64_t size, int mode64, int cmd) {
	uint32_t t32; uint64_t n64;
	struct {
		uint16_t name[36];
		uint32_t size, size_hi; uint64_t dummy;
	} pkt = { 0 };
	int ret;

	ret = copy_to_wstr(pkt.name, sizeof(pkt.name) / 2, name);
	if (ret) ERR_EXIT("name too long\n");
	n64 = size;
	WRITE32_LE(&pkt.size, n64);
	if (mode64) {
		t32 = n64 >> 32;
		WRITE32_LE(&pkt.size_hi, t32);
	}

	encode_msg(io, cmd, &pkt,
			sizeof(pkt.name) + (mode64 ? 16 : 4));
}

static uint64_t dump_partition(spdio_t *io,
		const char *name, uint64_t start, uint64_t len,
		const char *fn, unsigned step) {
	uint32_t n, nread, t32; uint64_t offset, n64;
	int ret, mode64 = (start + len) >> 32;
	FILE *fo;

	select_partition(io, name, start + len, mode64, BSL_CMD_READ_START);
	send_and_check(io);

	fo = fopen(fn, "wb");
	if (!fo) ERR_EXIT("fopen(dump) failed\n");

	for (offset = start; (n64 = start + len - offset); ) {
		uint32_t data[3];
		n = n64 > step ? step : n64;

		WRITE32_LE(data, n);
		WRITE32_LE(data + 1, offset);
		t32 = offset >> 32;
		WRITE32_LE(data + 2, t32);

		encode_msg(io, BSL_CMD_READ_MIDST, data, mode64 ? 12 : 8);
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
		offset += nread;
		if (n != nread) break;
	}
	DBG_LOG("dump_partition: %s+0x%llx, target: 0x%llx, read: 0x%llx\n",
			name, (long long)start, (long long)len,
			(long long)(offset - start));
	fclose(fo);

	encode_msg(io, BSL_CMD_READ_END, NULL, 0);
	send_and_check(io);
	return offset;
}

static uint64_t read_pactime(spdio_t *io) {
	uint32_t n, offset = 0x81400, len = 8;
	int ret; uint32_t data[2];
	unsigned long long time, unix;

	select_partition(io, "miscdata", offset + len, 0, BSL_CMD_READ_START);
	send_and_check(io);

	WRITE32_LE(data, len);
	WRITE32_LE(data + 1, offset);
	encode_msg(io, BSL_CMD_READ_MIDST, data, sizeof(data));
	send_msg(io);
	recv_msg(io);
	if ((ret = recv_type(io)) != BSL_REP_READ_FLASH)
		ERR_EXIT("unexpected response (0x%04x)\n", ret);
	n = READ16_BE(io->raw_buf + 2);
	if (n != len) ERR_EXIT("unexpected length\n");

	time = (uint32_t)READ32_LE(io->raw_buf + 4);
	time |= (uint64_t)READ32_LE(io->raw_buf + 8) << 32;

	unix = time ? time / 10000000 - 11644473600 : 0;
	// $ date -d @unixtime
	DBG_LOG("pactime = 0x%llx (unix = %llu)\n", time, unix);

	encode_msg(io, BSL_CMD_READ_END, NULL, 0);
	send_and_check(io);
	return time;
}

static int scan_xml_partitions(const char *fn, uint8_t *buf, size_t buf_size) {
	const char *part1 = "Partitions>";
	char *src, *p, name[36]; size_t size = 0;
	int part1_len = strlen(part1), found = 0, stage = 0;
	src = (char*)loadfile(fn, &size, 1);
	if (!src) ERR_EXIT("loadfile failed\n");
	src[size] = 0;
	p = src;
	for (;;) {
		int i, a = *p++, n; char c; long long size;
		if (a == ' ' || a == '\t' || a == '\n' || a == '\r') continue;
		if (a != '<') {
			if (!a) break;
			if (stage != 1) continue;
			ERR_EXIT("xml: unexpected symbol\n");
		}
		if (!memcmp(p, "!--", 3)) {
			p = strstr(p + 3, "--");
			if (!p || !((p[-1] - '!') | (p[-2] - '<')) || p[2] != '>')
				ERR_EXIT("xml: unexpected syntax\n");
			p += 3;
			continue;
		}
		if (stage != 1) {
			stage += !memcmp(p, part1, part1_len);
			if (stage > 2)
				ERR_EXIT("xml: more than one partition lists\n");
			p = strchr(p, '>');
			if (!p) ERR_EXIT("xml: unexpected syntax\n");
			p++;
			continue;
		}
		if (*p == '/' && !memcmp(p + 1, part1, part1_len)) {
			p = p + 1 + part1_len;
			stage++;
			continue;
		}
		i = sscanf(p, "Partition id=\"%35[^\"]\" size=\"%lli\"/%n%c", name, &size, &n, &c);
		if (i != 3 || c != '>')
			ERR_EXIT("xml: unexpected syntax\n");
		p += n + 1;
		if (buf_size < 0x4c)
			ERR_EXIT("xml: too many partitions\n");
		buf_size -= 0x4c;
		memset(buf, 0, 36 * 2);
		for (i = 0; (a = name[i]); i++) buf[i * 2] = a;
		if (!i) ERR_EXIT("empty partition name\n");
		WRITE32_LE(buf + 0x48, size);
		buf += 0x4c;
		DBG_LOG("[%d] %s, %d\n", found, name, (int)size);
		found++;
	}
	if (p - 1 != src + size) ERR_EXIT("xml: zero byte");
	if (stage != 2) ERR_EXIT("xml: unexpected syntax\n");
	free(src);
	return found;
}

static void partition_list(spdio_t *io, const char *fn, unsigned sh) {
	unsigned size, i, n; char name[37];
	int ret; FILE *fo = NULL; uint8_t *p;
	uint32_t mask = 0;

	encode_msg(io, BSL_CMD_READ_PARTITION, NULL, 0);
	send_msg(io);
	recv_msg(io);
	ret = recv_type(io);
	if (ret != BSL_REP_READ_PARTITION)
		ERR_EXIT("unexpected response (0x%04x)\n", ret);
	size = READ16_BE(io->raw_buf + 2);
	if (size % 0x4c)
		ERR_EXIT("not divisible by struct size (0x%04x)\n", size);
	n = size / 0x4c;
	if (strcmp(fn, "-")) {
		fo = fopen(fn, "wb");
		if (!fo) ERR_EXIT("fopen failed\n");
		fprintf(fo, "<Partitions>\n");
	}
	p = io->raw_buf + 4;
	for (i = 0; i < n; i++, p += 0x4c) {
		ret = copy_from_wstr(name, 36, (uint16_t*)p);
		if (ret) ERR_EXIT("bad partition name\n");
		size = READ32_LE(p + 0x48);
		mask |= size;
		DBG_LOG("[%d] %s, %u (%u)\n", i, name, size >> sh, size);
		if (fo) {
			fprintf(fo, "    <Partition id=\"%s\" size=\"", name);
			if (i + 1 == n) fprintf(fo, "0x%x\"/>\n", ~0);
			else fprintf(fo, "%u\"/>\n", size >> sh);
		}
	}
	if (mask & ((1 << sh) - 1)) {
		DBG_LOG("The unit size is wrong!\n");
		if (!(mask & 0x7f))
			DBG_LOG("The partition table probably uses 4KB sectors, use partition_list_4k command.\n");
		fprintf(fo, "<error>Wrong unit size, do not use this table!</error>\n");
	}
	if (fo) {
		fprintf(fo, "</Partitions>\n");
		fclose(fo);
	}
}

static void repartition(spdio_t *io, const char *fn) {
	uint8_t *buf = io->temp_buf;
	int n = scan_xml_partitions(fn, buf, 0xffff);
	// print_mem(stderr, io->temp_buf, n * 0x4c);
	check_confirm("repartition");
	encode_msg(io, BSL_CMD_REPARTITION, buf, n * 0x4c);
	send_and_check(io);
}

static void erase_partition(spdio_t *io, const char *name) {
	check_confirm("erase partition");
	select_partition(io, name, 0, 0, BSL_CMD_ERASE_FLASH);
	send_and_check(io);
}

static void load_partition(spdio_t *io, const char *name,
		const char *fn, unsigned step) {
	uint64_t offset, len, n64;
	unsigned mode64, n; int ret;
	FILE *fi;

	fi = fopen(fn, "rb");
	if (!fi) ERR_EXIT("fopen(load) failed\n");

	fseeko(fi, 0, SEEK_END);
	len = ftello(fi);
	fseek(fi, 0, SEEK_SET);
	DBG_LOG("file size : 0x%llx\n", (long long)len);

	mode64 = len >> 32;
	check_confirm("write partition");
	select_partition(io, name, len, mode64, BSL_CMD_START_DATA);
	send_and_check(io);

	for (offset = 0; (n64 = len - offset); offset += n) {
		n = n64 > step ? step : n64;
		if (fread(io->temp_buf, 1, n, fi) != n) 
			ERR_EXIT("fread(load) failed\n");
		encode_msg(io, BSL_CMD_MIDST_DATA, io->temp_buf, n);
		send_msg(io);
		recv_msg_timeout(io, 15000);
		if (!ret) ERR_EXIT("timeout reached\n");
		if ((ret = recv_type(io)) != BSL_REP_ACK) {
			DBG_LOG("unexpected response (0x%04x)\n", ret);
			break;
		}
	}
	DBG_LOG("load_partition: %s, target: 0x%llx, written: 0x%llx\n",
			name, (long long)len, (long long)offset);
	fclose(fi);
	encode_msg(io, BSL_CMD_END_DATA, NULL, 0);
	send_and_check(io);
}

static int64_t find_partition_size(spdio_t *io, const char *name) {
	uint32_t t32; uint64_t n64; long long offset = 0;
	int ret, i, start = 47;

	select_partition(io, name, 1ll << (start + 1), 1, BSL_CMD_READ_START);
	send_and_check(io);

	for (i = start; i >= 20; i--) {
		uint32_t data[3];
		n64 = offset + (1ll << i) - (1 << 20);
		WRITE32_LE(data, 4);
		WRITE32_LE(data + 1, n64);
		t32 = n64 >> 32;
		WRITE32_LE(data + 2, t32);

		encode_msg(io, BSL_CMD_READ_MIDST, data, sizeof(data));
		send_msg(io);
		recv_msg(io);
		ret = recv_type(io);
		if (ret != BSL_REP_READ_FLASH) continue;
		offset = n64 + (1 << 20);
	}
	DBG_LOG("partition_size: %s, 0x%llx\n", name, offset);
	encode_msg(io, BSL_CMD_READ_END, NULL, 0);
	send_and_check(io);
	return offset;
}

static uint32_t t117_exec_dist = 0;

static void t117_exec_workaround(spdio_t *io, uint32_t addr) {
	uint8_t *buf = io->temp_buf, *p;
	unsigned dist = t117_exec_dist;
	unsigned i, n = 252, skip = 3;

	addr += 0x200;
#define X(v) ((unsigned)((v) & 0xff) - 0x7d < 2)
	if (addr >= 0x40000 || X(addr) || X(addr >> 8))
		ERR_EXIT("unsupported exec address\n");
	if (dist >> 16) ERR_EXIT("unsupported dist\n");
	if (X(dist)) skip--;
	if (X(dist >> 8)) skip--;
#undef X

	p = buf;
	for (i = 0; i < skip; i++) *p++ = 0;
	for (i = 0; i < n; i++, p += 4) WRITE32_LE(p, addr);
	if (skip & 1) *p++ = 0; // align
	encode_msg(io, dist, buf, p - buf);
	send_msg(io); // no ack
}

static uint64_t str_to_size(const char *str) {
	char *end; int shl = 0; uint64_t n;
	if ((unsigned)(*str - '0') >= 10)
		ERR_EXIT("bad command args\n");
	n = strtoull(str, &end, 0);
	if (*end) {
		if (!strcmp(end, "K")) shl = 10;
		else if (!strcmp(end, "M")) shl = 20;
		else if (!strcmp(end, "G")) shl = 30;
		else ERR_EXIT("unknown size suffix\n");
	}
	if (shl) {
		int64_t tmp = n;
		tmp >>= 63 - shl;
		if (tmp && ~tmp)
			ERR_EXIT("size overflow on multiply\n");
	}
	return n << shl;
}

static uint64_t str_to_addr(const char *str, const char *name, uint32_t base) {
	uint32_t sh = 32; uint64_t ret;
	int a, i = 0;
	do if (!(a = name[i])) {
		str += i;
		if (base == ~0u)
			ERR_EXIT("%s address is unknown\n", name);
		if (!(a = *str++)) return base;
		if (a != '+') ERR_EXIT("bad command args\n");
		sh = 26; /* 64MB */
		goto found;
	} while (a == str[i++]);
	base = 0;
found:
	ret = str_to_size(str);
	if (ret >> sh)
		ERR_EXIT("address limit reached\n");
	return ret + base;
}

#define REOPEN_FREQ 2

int main(int argc, char **argv) {
#if USE_LIBUSB
	libusb_device_handle *device;
#else
	int serial;
#endif
	spdio_t *io; int ret, i;
	int wait = 300 * REOPEN_FREQ;
	const char *tty = "/dev/ttyUSB0";
	int verbose = 0, fdl_loaded = 0;
	uint32_t fw_addr = ~0u;
	int keep_charge = 0, end_data = 1, blk_size = 0;

#if USE_LIBUSB
	(void)tty;
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
			addr = str_to_addr(argv[3], "ram", fw_addr | 0x04000000);

			if (!fdl_loaded) {
				// Required for smartphones.
				// Is there a way to do the same with usb-serial?
#if USE_LIBUSB
				ret = libusb_control_transfer(io->dev_handle,
						0x21, 34, 0x601, 0, NULL, 0, io->timeout);
				if (ret < 0)
					ERR_EXIT("libusb_control_transfer failed : %s\n",
							libusb_error_name(ret));
#endif
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
				send_and_check(io);

				send_file(io, fn, addr, end_data,
					blk_size ? blk_size : 528, 0, 0);

				if (t117_exec_dist) {
					t117_exec_workaround(io, addr);
				} else {
					encode_msg(io, BSL_CMD_EXEC_DATA, NULL, 0);
					send_and_check(io);
				}

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

				{
					char *str = (char*)io->raw_buf + 4;
					int len = READ16_BE(io->raw_buf + 2);
					DBG_LOG("BSL_REP_VER: ");
					print_string(stderr, str, len);
					if (len && !str[len - 1]) {
						if (strstr(str, "CHIP ID = 0x6530") ||
								strstr(str, "CHIP ID = 0x6531")) fw_addr = 0x30000000;
						if (strstr(str, "CHIP ID = 0x6562")) fw_addr = 0x10000000;
					}
				}

				encode_msg(io, BSL_CMD_CONNECT, NULL, 0);
				send_and_check(io);

				if (keep_charge) {
					encode_msg(io, BSL_CMD_KEEP_CHARGE, NULL, 0);
					send_and_check(io);
				}

			} else {

				send_file(io, fn, addr, end_data,
					blk_size ? blk_size : 528, 0, 0);

				encode_msg(io, BSL_CMD_EXEC_DATA, NULL, 0);
				send_msg(io);
				// Feature phones respond immediately,
				// but it may take a second for a smartphone to respond.
				ret = recv_msg_timeout(io, 15000);
				if (!ret) ERR_EXIT("timeout reached\n");
				ret = recv_type(io);
				// Is it always bullshit?
				if (ret == BSL_REP_INCOMPATIBLE_PARTITION)
					DBG_LOG("FDL2: incompatible partition\n");
				else if (ret != BSL_REP_ACK)
					ERR_EXIT("unexpected response (0x%04x)\n", ret);
			}

			fdl_loaded++;
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "read_flash")) {
			const char *fn; uint64_t addr, offset, size;
			int mode = 0;
			if (argc <= 5) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			offset = str_to_size(argv[3]);
			if (!offset && !strcmp(argv[4], "auto")) mode = 1, size = 0;
			else size = str_to_size(argv[4]);
			fn = argv[5];
			if ((addr | size | offset | (addr + offset + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			dump_flash(io, addr, offset, size, fn,
					blk_size ? blk_size : 1024, mode);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "write_word")) {
			uint64_t addr, data; uint8_t buf[4];
			if (argc <= 3) ERR_EXIT("bad command\n");

			addr = str_to_addr(argv[2], "fw", fw_addr);
			data = strtoull(argv[3], NULL, 0);
			WRITE32_LE(buf, data);
			if (addr >> 32)
				ERR_EXIT("32-bit limit reached\n");
			send_buf(io, addr, end_data, 528, buf, 4);
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "write_data")) {
			const char *fn; uint64_t addr, offset, size;
			if (argc <= 5) ERR_EXIT("bad command\n");

			addr = str_to_addr(argv[2], "fw", fw_addr);
			offset = str_to_size(argv[3]);
			size = str_to_size(argv[4]);
			fn = argv[5];
			if ((addr | size | offset | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			send_file(io, fn, addr, end_data,
				blk_size ? blk_size : 528, offset, size);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "erase_flash")) {
			uint64_t addr, size;
			if (argc <= 3) ERR_EXIT("bad command\n");

			addr = str_to_addr(argv[2], "fw", fw_addr);
			size = str_to_size(argv[3]);
			if ((addr | size | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			erase_flash(io, addr, size);
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "read_mem")) {
			const char *fn; uint64_t addr, size;
			if (argc <= 4) ERR_EXIT("bad command\n");

			addr = str_to_size(argv[2]);
			size = str_to_size(argv[3]);
			fn = argv[4];
			if ((addr | size | (addr + size)) >> 32)
				ERR_EXIT("32-bit limit reached\n");
			dump_mem(io, addr, size, fn,
					blk_size ? blk_size : 1024);
			argc -= 4; argv += 4;

		} else if (!strcmp(argv[1], "part_size")) {
			const char *name;
			if (argc <= 2) ERR_EXIT("bad command\n");

			name = argv[2];
			find_partition_size(io, name);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "read_part")) {
			const char *name, *fn; uint64_t offset, size;
			if (argc <= 5) ERR_EXIT("bad command\n");

			name = argv[2];
			offset = str_to_size(argv[3]);
			size = str_to_size(argv[4]);
			fn = argv[5];
			if (offset + size < offset)
				ERR_EXIT("64-bit limit reached\n");
			dump_partition(io, name, offset, size, fn,
					blk_size ? blk_size : 4096);
			argc -= 5; argv += 5;

		} else if (!strcmp(argv[1], "partition_list")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			partition_list(io, argv[2], 10);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "partition_list_4k")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			partition_list(io, argv[2], 7);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "repartition")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			repartition(io, argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "erase_part")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			erase_partition(io, argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "write_part")) {
			if (argc <= 3) ERR_EXIT("bad command\n");
			load_partition(io, argv[2], argv[3],
					blk_size ? blk_size : 4096);
			argc -= 3; argv += 3;

		} else if (!strcmp(argv[1], "read_pactime")) {
			read_pactime(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "blk_size")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			blk_size = strtol(argv[2], NULL, 0);
			blk_size = blk_size < 0 ? 0 :
					blk_size > 0xffff ? 0xffff : blk_size;
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "chip_uid")) {
			encode_msg(io, BSL_CMD_READ_CHIP_UID, NULL, 0);
			send_msg(io);
			ret = recv_msg(io);
			if ((ret = recv_type(io)) != BSL_REP_READ_CHIP_UID)
				ERR_EXIT("unexpected response (0x%04x)\n", ret);

			DBG_LOG("BSL_REP_READ_CHIP_UID: ");
			print_string(stderr, io->raw_buf + 4, READ16_BE(io->raw_buf + 2));
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "disable_transcode")) {
			encode_msg(io, BSL_CMD_DISABLE_TRANSCODE, NULL, 0);
			send_and_check(io);
			io->flags &= ~FLAGS_TRANSCODE;
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "transcode")) {
			unsigned a, f;
			if (argc <= 2) ERR_EXIT("bad command\n");
			a = atoi(argv[2]);
			if (a >> 1) ERR_EXIT("bad command\n");
			f = (io->flags & ~FLAGS_TRANSCODE);
			io->flags = f | (a ? FLAGS_TRANSCODE : 0);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "keep_charge")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			keep_charge = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "timeout")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			io->timeout = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "end_data")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			end_data = atoi(argv[2]);
			argc -= 2; argv += 2;

		} else if (!strcmp(argv[1], "reset")) {
			encode_msg(io, BSL_CMD_NORMAL_RESET, NULL, 0);
			send_and_check(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "power_off")) {
			encode_msg(io, BSL_CMD_POWER_OFF, NULL, 0);
			send_and_check(io);
			argc -= 1; argv += 1;

		} else if (!strcmp(argv[1], "t117_exec_dist")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			t117_exec_dist = strtol(argv[2], NULL, 0);
			argc -= 2; argv += 2;

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
