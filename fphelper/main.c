#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#if WITH_LZMADEC
static int lzma_sprd = 0;
#define LZMA_SPRD_HACK lzma_sprd
#include "lzma/LzmaDecode.c"

static size_t decode_lzma_impl(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	size_t inSizeProcessed = 0, outSizeProcessed = 0;
	size_t size = *src_size; int ret;
	if (size > 5 + 8) do {
		CLzmaDecoderState decoder = { 0 };
		ret = LzmaDecodeProperties(&decoder.Properties, src, size);
		if (ret != LZMA_RESULT_OK) break;
		ret = LzmaGetNumProbs(&decoder.Properties);
		decoder.Probs = malloc(sizeof(CProb) * ret);
		if (!decoder.Probs) break;
		ret = LzmaDecode(&decoder,
				src + 5 + 8, size - 5 - 8, &inSizeProcessed,
				dst, dst_size, &outSizeProcessed);
		free(decoder.Probs);
		inSizeProcessed += 5 + 8;
	} while (0);
	*src_size = inSizeProcessed;
	return outSizeProcessed;
}

static size_t decode_lzma(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	lzma_sprd = 0;
	return decode_lzma_impl(src, src_size, dst, dst_size);
}

static size_t sprd_lzmadec(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	lzma_sprd = 1;
	return decode_lzma_impl(src, src_size, dst, dst_size);
}
#endif

#define END() do { \
	*src_size = src - src_start; \
	return dst - dst_start; \
} while (0)

#define IN(x) do { \
	if (src == src_end) END(); \
	x = *src++; \
} while (0)

#define OUT(x) do { \
	if (dst_start) { \
		if (dst == dst_end) END(); \
		*dst = x; \
	} \
	dst++; \
} while (0)

#define CHECK_DIST(x) do { \
	if ((x) > dst - dst_start) END(); \
} while (0)

static size_t decode_copy(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	size_t n = *src_size;
	if (n > dst_size) n = dst_size;
	if (dst) memcpy(dst, src, n);
	*src_size = n;
	return n;
}

static size_t decode_zero(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	if (dst) memset(dst, 0, dst_size);
	*src_size = 0;
	return dst_size;
}

static size_t sprd_lzdec3(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	const uint8_t *src_end = src + *src_size, *src_start = src;
	uint8_t *dst_end = dst + dst_size, *dst_start = dst;
	do {
		int tmp, code, skip, len, dist;
		IN(code);
		skip = code & 7; // 3 bits
		if (!skip) {
			IN(skip);
			if (!skip) END(); // error
		}

		len = code >> 4;
		if (!len) IN(len);
		while (--skip) { IN(tmp); OUT(tmp); }

		if (code & 8) {
			len++;
			IN(dist);
			CHECK_DIST(dist);
			do OUT(dst[-dist]); while (len--);
		} else {
			while (len--) OUT(0);
		}
	} while (dst < dst_end);
	END();
}

static size_t sprd_lzdec2(const uint8_t *src, size_t *src_size, uint8_t *dst, size_t dst_size) {
	const uint8_t *src_end = src + *src_size, *src_start = src;
	uint8_t *dst_end = dst + dst_size, *dst_start = dst;
	do {
		int tmp, code, skip, len, dist;
		IN(code);
		skip = code & 3; // 2 bits
		if (!skip) {
			IN(skip);
			if (!skip) END(); // error
		}

		len = code >> 4;
		if (!len) IN(len);
		while (--skip) { IN(tmp); OUT(tmp); }
		if (len) {
			IN(dist);
			tmp = code >> 2 & 3;
			if (tmp == 3) IN(tmp);
			dist |= tmp << 8;
			len++;
			CHECK_DIST(dist);
			do OUT(dst[-dist]); while (len--);
		}
	} while (dst < dst_end);
	END();
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

#define READ32_LE(p) ( \
	((uint8_t*)(p))[0] | \
	((uint8_t*)(p))[1] << 8 | \
	((uint8_t*)(p))[2] << 16 | \
	((uint8_t*)(p))[3] << 24)

static uint32_t print_init_table(uint8_t *buf, unsigned size, uint32_t o1, int flags) {
	uint32_t *p = (uint32_t*)(buf + o1), *p2;
	uint32_t o2, o3, i, n, size2;
	uint32_t lz_addr = 0, lz_type = -1;
	uint32_t copy_addr = 0, zero_addr = 0;
	uint32_t fwaddr = 0, found_fwaddr = 0;

	size2 = size - o1 - 8;
	o2 = *p++; o3 = *p++;
	if (o3 > (size - o1)) return 0;
	o2 += o1; o3 += o1;
	printf("0x%x: init_table, start = 0x%x, end = 0x%x\n", o1, o2, o3);

	if (size2 >= 0x10 && p[0] == 0xe28fc001) {
		unsigned n = 0;
		lz_addr = (uint8_t*)p - buf;
		if (p[3] == 0x075c3001) lz_type = 3, n = 0x5c;
		if (p[3] == 0x079c3001) lz_type = 2, n = 0x60;
		if (n) printf("0x%x: init_lzdec%u\n", lz_addr, lz_type);
		if (size2 < n) return 0;
		size2 -= n; p += n >> 2;
	}
	if (size2 >= 0x28 && p[0] == 0xe2522010) {
		unsigned n = 0x28;
		copy_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("0x%x: init_copy\n", copy_addr);
	}
	if (size2 >= 0x38 && p[0] == 0xe3b03000) {
		unsigned n = 0x38;
		zero_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("0x%x: init_zero\n", zero_addr);
	}

	n = o3 - o2;
	if (o2 >= o3 || (n & 0xf) || ((n >> 4) - 1) >= 20) return 0;
	p = (uint32_t*)(buf + o2);
	n >>= 4;

	{
		uint32_t buf[3] = { lz_addr, copy_addr, zero_addr };
		unsigned j, k, x = 0;
		for (j = 0; j < 3; j++) {
			uint32_t a = buf[j];
			if (!a) continue;
			a = p[3] - a;
			for (i = 0; i < n; i++) {
				for (k = 0; k < 3; k++)
					if (buf[k] && p[i * 4 + 3] - a == buf[k]) break;
				if (k == 3) break;
			}
			if (i == n) x |= 1 << j, fwaddr = a;
		}
		if (x && !(x & (x - 1))) found_fwaddr = 1;
	}

	for (p2 = p, i = 0; i < n; i++, p2 += 4)
		printf("%u: src = 0x%x, dst = 0x%08x, len = 0x%x, fn = 0x%x\n",
				i, p2[0], p2[1], p2[2], p2[3]);
	printf("\n");

	if (found_fwaddr) {
		uint32_t ps_size = size;
		for (p2 = p, i = 0; i < n; i++, p2 += 4) {
			uint32_t a = p2[0];
			if (zero_addr && a == zero_addr) continue;
			if (a < fwaddr) continue;
			a -= fwaddr;
			if (ps_size > a) ps_size = a;
		}
		printf("ps_addr: 0x%x\n", fwaddr);
		printf("ps_size: 0x%x\n", ps_size);
		return ps_size;
	}
	return 0;
}

static void id2str(char *buf, uint32_t val) {
	int i;
	for (i = 0; i < 4; i++) {
		int a = val >> 24; val <<= 8;
		if (a >= 32 && a < 127) *buf++ = a;
		else sprintf(buf, "\\x%02x", a), buf += 4;
	}
	*buf = 0;
}

static int drps_decode(uint8_t *mem, size_t size,
		unsigned drps_offs, unsigned index, const char *outfn);

static void scan_fw(uint8_t *buf, unsigned size, int flags) {
	unsigned i, size_req = 0x1c;
	unsigned size2, ps_size;
	char name[128], drps_cnt[4] = { 0 };
	if (size >= 0x24 && *(uint32_t*)(buf + 0x20) == 0x36353632)
		printf("0x20: found SC6531E firmware marker\n");
	size &= ~3;
	if (size < size_req) return;
	ps_size = size;
	for (i = 0; i < size - size_req + 1; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);
		do {
			if (p[0] != 0xe8ba000f) break;
			if (p[1] != 0xe24fe018) break;
			if (p[2] != 0xe3130001) break;
			if (p[3] != 0x1047f003) break;
			if (p[4] != 0xe12fff13) break;
			{
				uint32_t a = print_init_table(buf, size, i + 0x14, flags);
				// use the last one found, in case the firmware
				// has an additional bootloader at the beginning
				if (a) ps_size = a;
			}
		} while (0);

		do {
			if (p[0] != 0x50415254) break;
			if (p[1] != 0x494d4147) break;
			printf("0x%x: TRAPGAMI, kern = 0x%x, user = 0x%x\n", i, p[2], p[3]);
		} while (0);

		do {
			uint32_t *p2, j, n;
			if (p[0] != 0x53505244) break;
			if (p[1] != 0) break;
			n = p[3];
			if ((n - 1) >> 5) break;
			printf("0x%x: DRPS, size = 0x%x, num = %u\n", i, p[2], n);
			size2 = size - i;
			if (size2 < p[2]) break;
			size2 -= p[2];
			p2 = (uint32_t*)((uint8_t*)p + p[2]);
			if (ps_size > i) ps_size = i;
			for (j = 0; j < n; j++, p2 += 5) {
				if (size2 < 0x14) break;
				size2 -= 0x14;
				if (p2[0] != 0x424c4f43) break;
				id2str(name, p2[1]);
				printf("0x%x: COLB, name = \"%s\", offs = 0x%x (0x%x), size = 0x%x, 0x%x\n",
						(unsigned)((uint8_t*)p2 - buf), name, p2[2], i + p2[2], p2[3], p2[4]);
#if WITH_LZMADEC
				if (flags & 1) {
					const char *s = "unknown"; int k = 0;
					switch (p2[1]) {
					case 0x494d4147: s = "kern"; k = 1; break;
					case 0x75736572: s = "user"; k = 2; break;
					case 0x7253736f: s = "rsrc"; k = 3; break;
					}
					k = drps_cnt[k]++;
					if (!k) snprintf(name, sizeof(name), "%s.bin", s);
					else snprintf(name, sizeof(name), "%s%u.bin", s, k);
					drps_decode(buf, size, i, j, name);
				}
#endif
			}
		} while (0);

		do {
			uint32_t *p2 = p + 6, a;
			if ((p[0] ^ 0x8c000000) >> 12) break;
			if ((p[2] ^ 0x8c000000) >> 12) break;
			if ((p[4] ^ 0x8c000000) >> 12) break;
			size2 = ((size - i) >> 3) - 3;
			for (; size2--; p2 += 2) {
				if ((a = p2[0]) == ~0u && p2[1] == ~0u) {
					unsigned end = (uint8_t*)(p2 + 2) - buf;
					printf("pinmap: 0x%x-0x%x\n", i, end);
					i = end - 4;
					break;
				}
				if ((a ^ 0x8c000000) >> 12 && (a ^ 0x82001000) >> 12) break;
			}
		} while (0);
	}
	if ((flags & 1) && ps_size < size) {
		FILE *fo = fopen("ps.bin", "wb");
		if (!fo) fprintf(stderr, "fopen(output) failed\n");
		else {
			fwrite(buf, 1, ps_size, fo);
			fclose(fo);
		}
	}
}

static uint32_t storage_chk(const uint16_t *p, unsigned n, uint32_t pos) {
	uint32_t sum = pos + 0xc513;
	for (; n > 1; n -= 2) sum += *p++;
	return sum & 0xffff;
}

static unsigned fat12_size(uint8_t *p) {
	do {
		if (memcmp(p + 0x36, "FAT12   ", 8)) break;
		return p[0x13] | p[0x14] << 8;
	} while (0);
	return 0;
}

static int decode_imei(uint8_t *p, uint8_t *d) {
	unsigned a, i;
	if ((*p & 15) != 10) return 0;
	for (i = 1; i < 16; i++) {
		a = p[i >> 1];
		if (i & 1) a >>= 4;
		a &= 15;
		if (a >= 10) return 0;
		*d++ = '0' + a;
	}
	*d = 0;
	return 15;
}

static void prodinfo_str(uint8_t *buf, int n) {
	int i;
	for (i = 0; i < n; i++) {
		int a = buf[i];
		if (!a) break;
		if (a >= 32 && a < 127) putchar(a);
		else printf("\\x%02x", a);
	}
}

static void safe_print_utf16(unsigned a) {
	unsigned b;
	if (a >= 0x80) {
		if (a >= 0x800) {
			if ((a - 0xd800) < 0x800) {
				printf("\\u%x", a);
				return;
			}
			putchar(a >> 12 | 0xe0);
			b = (a >> 6 & 0x3f) | 0x80;
		} else b = a >> 6 | 0xc0;
		putchar(b);
		a = (a & 0x3f) | 0x80;
	} else {
		if (a < 0x20) {
			if (a == '\n') printf("\\n");
			else printf("\\x%02x", a);
			return;
		}
		if (a == '"') putchar('\\');
	}
	putchar(a);
}

static uint8_t* sms_addr_decode(uint8_t *d, uint8_t *p) {
	unsigned a, b, i, k, n;
	const char *conv = "0123456789*#abc";
	n = *p++; /* sender length */
	if ((n - 1) >= 20) return NULL;
	// len -= (n + 1) >> 1;
	a = *p++; /* sender type */
	if (a == 0xd0) {
		for (a = i = k = 0; i < n; i++) {
			if (!(i & 1)) a |= *p++ << k, k += 8;
			if (k >= 7) {
				if ((b = a & 127) < 0x20) return NULL;
				*d++ = b; a >>= 7; k -= 7;
			}
		}
	} else if ((a | 0x10) == 0x91) {
		if (a == 0x91) *d++ = '+';
		for (i = 0; i < n; i++) {
			a = *p++; k = a & 15; a >>= 4;
			if (k > 14) return NULL;
			*d++ = conv[k];
			if (++i >= n) break;
			if (a > 14) return NULL;
			*d++ = conv[a];
		}
	} else return NULL;
	*d = 0;
	return p;
}

static void sms_decode(uint8_t *p, unsigned len) {
	uint8_t buf[22], *end = p + len;
	//print_mem(stdout, p, len);
	do {
		unsigned a, k, i, n, dcs, pdu_type;
		p += 0x12; /* is it constant? */
		pdu_type = *p++;
		if (pdu_type & 2) break; /* unknown message type */
		if (pdu_type & 1) { /* sms-submit */
			p++; /* sms-sequence */
		}
		p = sms_addr_decode(buf, p);
		if (!p) break;
		printf("sms %s: %s\n", pdu_type & 1 ? "receiver" : "sender", buf);

		dcs = p[1]; // Data Coding Scheme

		if (p[2] != 0xff) {
			for (i = 0; i < 7; i++) {
				a = p[2 + i]; k = a & 15; a >>= 4;
				if ((i < 6 && k > 9) || a > 9) return;
				buf[i] = a + k * 10;
			}
			a = buf[0]; k = buf[6]; n = '+';
			if (k >= 80) k -= 80, n = '-';
			printf("sms time stamp: %u.%02u.%02u %02u:%02u:%02u GMT%c%u:%02u\n",
					a + (a < 80 ? 2000 : 1900), buf[1], buf[2],
					buf[3], buf[4], buf[5], n, k >> 2, (k & 3) * 15);
			p += 6;
		}
		n = p[3]; p += 4;
		// printf("sms len: %u\n", n);
		len = end - p;

		n *= dcs & 0xc ? 8 : 7;
		if (len < (n + 7) >> 3) break;

		// some SMS contain UDH even without this bit
		if (n >= 6 * 8 && p[0] == 5 && p[1] == 0 && p[2] == 3)
			pdu_type |= 0x40;

		k = 0; // padding bits for 7-bit encoding

		// User Data Header
		if (pdu_type & 0x40) {
			if (n < 8 || n < (a = p[0] + 1) * 8) break;
			if (p[1] == 0 && p[2] == 3)
				printf("sms part: %u / %u (ref = 0x%02x)\n", p[5], p[4], p[3]);
			if (p[1] == 8 && p[2] == 4)
				printf("sms part: %u / %u (ref = 0x%04x)\n", p[6], p[5], p[3] << 8 | p[4]);
			n -= a * 8; p += a;
			if (!(dcs & 0xc) && (a %= 7)) k = 7 - a;
		}

		if (dcs & 0xc) n >>= 3;
		if ((dcs & 0xc) == 8 && (n & 1)) break;

		printf("sms message: \"");
		if ((dcs & 0xc) == 0) { // 7-bit
			// skip padding if necessary
			a = 0; if (k) a = *p++ >> k, k = 8 - k;
			for (i = 0; i < n; i += 7) {
				if (k < 7) a |= *p++ << k, k += 8;
				safe_print_utf16(a & 127); a >>= 7; k -= 7;
			}
		} else if ((dcs & 0xc) == 4) { // 8-bit
			for (i = 0; i < n; i++)
				safe_print_utf16(*p++);
		} else if ((dcs & 0xc) == 8) { // utf16
			for (i = 0; i < n; i += 2) {
				a = p[0] << 8 | p[1]; p += 2;
				safe_print_utf16(a);
			}
		}
		printf("\"\n");
	} while (0);
}

static void scan_data(uint8_t *buf, unsigned size, int flags) {
	unsigned i, size_req = 0x10;
	unsigned size2;
	size &= ~3;
	for (i = 0; i < size - size_req + 1; i += 4) {
		if (!(i & 0xff)) do {
			uint8_t *p2 = (uint8_t*)(buf + i);
			unsigned a, j, n;
			size2 = size - i;
			if (size2 < 0x100) break;
			a = *(uint32_t*)p2;
			if (a != 0x53503039) break; // 90PS
#if 0
			a = *(uint32_t*)(p2 + 0xfc);
			if ((a & 0x7ff0fff0) != 0x7ff0fff0) break;
#endif
			if (*(uint32_t*)(p2 + 0x34) > 10) break;
			if (*(uint32_t*)(p2 + 0xdc) != 0x53534150) break;
			printf("0x%x: prodinfo, end = 0x%08x\n", i, *(uint32_t*)(p2 + 0xfc));
			printf("prodinfo serial: \""); prodinfo_str(p2 + 4, 0x30); printf("\"\n");
			n = *(uint32_t*)(p2 + 0x34);
			if (n) {
				printf("prodinfo features:\n");
				for (j = 0; j < n; j++) {
					printf("%u: \"", j); prodinfo_str(p2 + 0x38 + j * 10, 10); printf("\"\n");
				}
				printf("\n");
			}
		} while (0);

		if (!(i & 0xfff)) do {
			uint16_t *p2 = (uint16_t*)(buf + i);
			unsigned a, b, n, items, j;
			// id, checksum, id2, size
			a = p2[0]; b = p2[2]; n = p2[3];
			size2 = size - i;
			if ((b - 1) >= 0xfffe || (a && a != b) || !n || size2 < n + 8) break;
			if (p2[1] != storage_chk(p2 + 2, n + 4, i)) break;

			for (items = 0, j = i; size2 >= 8; items++, j += n) {
				p2 = (uint16_t*)(buf + j);
				a = p2[0]; b = p2[2]; n = p2[3];
				if ((b - 1) >= 0xfffe || (a && a != b) || !n) break;
				n = 8 + ((n + 1) & ~1);
				if (size2 < n) break;
				size2 -= n;
				a = storage_chk(p2 + 2, n - 4, j);
				if (p2[1] != a) break;
			}
			n = b = j; a = 0x200; n = 0;
			for (;; j += a) {
				j = (j + a - 1) & -a;
				if (size < j) break;
				if (*(uint16_t*)(buf + j - 2) == 0x55aa) {
					n = j - i; break;
				}
			}
			printf("0x%x: storage block, items = %u, size = 0x%x", i, items, b - i);
			if (n) printf(" / 0x%x", n);
			printf("\n");

			for (j = i; items--; j += n) {
				p2 = (uint16_t*)(buf + j);
				a = p2[0]; b = p2[2]; n = p2[3];
				printf("0x%x: %sid = %u, size = %u\n", j, !a ? "deleted, " : "", b, p2[3]);
				// FAT id: 2900, 5000, 5100, 6280
				if (n && !(n & 511)) {
					a = fat12_size((uint8_t*)p2 + 8);
					if (a) printf("0x%x: FAT12 header, sectors = %u\n", j + 8, a);
				} else if (n == 232) {
					sms_decode((uint8_t*)p2 + 8, 232);
				// IMEI id: 5, 377, 390, 484
				} else if (n == 8) {
					uint8_t imei[16];
					if (decode_imei((uint8_t*)p2 + 8, imei))
						printf("0x%x: IMEI = %s\n", j + 8, imei);
				}
				n = 8 + ((n + 1) & ~1);
			}
			i = ((j + 3) & ~3) - 4;
		} while (0);
	}
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); return 1; } while (0)

static int run_decoder(uint8_t *mem, size_t size, int argc, char **argv,
		size_t (*decode_fn)(const uint8_t*, size_t*, uint8_t*, size_t)) {
	size_t src_addr, dst_size, src_size, result;
	const char *outfn; uint8_t *dst; FILE *fo;

	if (argc <= 4) ERR_EXIT("bad command\n");

	src_addr = strtol(argv[2], NULL, 0);
	dst_size = strtol(argv[3], NULL, 0);
	outfn = argv[4];

	if (src_addr >= size) ERR_EXIT("addr >= filesize\n");
	src_size = size - src_addr;

	dst = malloc(dst_size);
	if (!dst) ERR_EXIT("malloc failed\n");
	result = decode_fn(mem + src_addr, &src_size, dst, dst_size);
	printf("src: 0x%zx-0x%zx, size = 0x%zx; dst: size = %zd / %zd\n",
		src_addr, src_addr + src_size, src_size, result, dst_size);

	fo = fopen(outfn, "wb");
	if (!fo) fprintf(stderr, "fopen(output) failed\n");
	else {
		fwrite(dst, 1, result, fo);
		fclose(fo);
	}
	free(dst);
	return 0;
}

#define FATAL() do { \
	fprintf(stderr, "!!! error at %s:%u\n", __func__, __LINE__); \
	goto err; \
} while (0)

#if WITH_LZMADEC
static int drps_decode(uint8_t *mem, size_t size,
		unsigned drps_offs, unsigned index, const char *outfn) {
	size_t src_addr, src_size, result, dst_size;
	uint8_t *dst = NULL; FILE *fo = NULL;
	unsigned drps_num, drps_size, i;
	uint32_t *p, offs;

	if (size < drps_offs) FATAL();
	size -= drps_offs;
	if (size < 0x10) FATAL();
	mem += drps_offs;
	p = (uint32_t*)mem;
	if (p[0] != 0x53505244) FATAL();
	drps_size = p[2];
	if (size < drps_size) FATAL();
	size -= drps_size;

	drps_num = p[3];
	if ((drps_num - 1) >> 8) FATAL();
	if (index >= drps_num) FATAL();
	if (size < (index + 1) * 0x14) FATAL();

	p = (uint32_t*)(mem + drps_size);
	for (i = 0; i <= index; i++, p += 5)
		if (p[0] != 0x424c4f43) FATAL();
	p -= 5;
	dst_size = p[4];
	if ((dst_size - 1) >> 28) FATAL();
	dst = malloc(dst_size);
	if (!dst) ERR_EXIT("malloc failed\n");
	{
		size_t size2;
		uint32_t colb_offs = p[2];
		uint32_t colb_size = p[3];
		size_t src_addr = drps_offs + colb_offs;
		if (colb_offs < 0x10) FATAL();
		if (colb_size < 5 + 8) FATAL();
		if (colb_offs > drps_size) FATAL();
		drps_size -= colb_offs;
		if (drps_size < colb_size) FATAL();
		mem += colb_offs;
		p = (uint32_t*)mem;

		fo = fopen(outfn, "wb");
		if (!fo) ERR_EXIT("fopen(output) failed\n");

		if (*p == 0x4e504143) {
			uint32_t data_size = p[2], num, offs, next;
			if (colb_size < 0x10) FATAL();
			num = p[3];
			printf("0x%x: CAPN, 0x%x, size = 0x%x, num = %u\n",
					(unsigned)src_addr, p[1], data_size, num);
			if (data_size < 0x10 || (num - 1) >> 24) FATAL();
			if (colb_size < data_size) FATAL();
			colb_size -= data_size;
			if (colb_size != num * 4) FATAL();
			p = (uint32_t*)((uint8_t*)p + data_size);
			offs = p[0];
			if (offs < 0x10) FATAL();
			for (i = 0; i < num; i++, offs = next) {
				size_t size2;
				next = i + 1 < num ? p[i + 1] : data_size;
				if (next < offs || next > data_size) FATAL();
				src_size = next - offs;

#define RUN_LZMADEC(mem) \
	if (src_size < 5 + 8) FATAL(); \
	size2 = READ32_LE(mem + 5 + 4); \
	if (size2) FATAL(); \
	size2 = READ32_LE(mem + 5); \
	if (size2 > dst_size) FATAL(); \
	result = sprd_lzmadec(mem, &src_size, dst, size2); \
	fwrite(dst, 1, result, fo);

				RUN_LZMADEC(mem + offs)
				if (result != size2) {
					printf("src: 0x%zx-0x%zx, size = 0x%zx; dst: size = %zd / %zd\n",
							src_addr + offs, src_addr + offs + src_size, src_size, result, size2);
					FATAL();
				}
			}
		} else {
			src_size = colb_size;
			RUN_LZMADEC(mem)
			printf("src: 0x%zx-0x%zx, size = 0x%zx; dst: size = %zd / %zd\n",
					src_addr, src_addr + src_size, src_size, result, size2);
#undef RUN_LZMADEC
		}
		fclose(fo);
	}
	free(dst);
	return 0;
#undef FATAL
err:
	if (fo) fclose(fo);
	if (dst) free(dst);
	return 1;
}
#endif

int main(int argc, char **argv) {
	uint8_t *mem; size_t size = 0;

	if (argc < 3)
		ERR_EXIT("Usage: %s flash.bin cmd args...\n", argv[0]);

	mem = loadfile(argv[1], &size);
	if (!mem) ERR_EXIT("loadfile failed\n");
	argc -= 1; argv += 1;

	while (argc > 1) {
		if (!strcmp(argv[1], "scan")) {
			scan_fw(mem, size, 0);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "scan_data")) {
			scan_data(mem, size, 0);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "copy")) {
			if (run_decoder(mem, size, argc, argv, &decode_copy)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "lzdec2")) {
			if (run_decoder(mem, size, argc, argv, &sprd_lzdec2)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "lzdec3")) {
			if (run_decoder(mem, size, argc, argv, &sprd_lzdec3)) return 1;
			argc -= 4; argv += 4;
#if WITH_LZMADEC
		} else if (!strcmp(argv[1], "unpack")) {
			scan_fw(mem, size, 1);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "lzmadec")) {
			if (run_decoder(mem, size, argc, argv, &decode_lzma)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "lzmadec_sprd")) {
			if (run_decoder(mem, size, argc, argv, &sprd_lzmadec)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "drps_dec")) {
			unsigned offs, index; const char *outfn;
			if (argc <= 4) ERR_EXIT("bad command\n");
			offs = strtol(argv[2], NULL, 0);
			index = strtol(argv[3], NULL, 0);
			outfn = argv[4];
			if (drps_decode(mem, size, offs, index, outfn)) return 1;
			argc -= 4; argv += 4;
#endif
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

