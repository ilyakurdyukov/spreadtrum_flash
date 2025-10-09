#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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

static struct {
	uint32_t ps_addr;
	uint32_t pinmap_offs;
	uint32_t keymap_addr;
	uint32_t tfboot_offs, tfboot_size;
} clues = { 0 };

static void save_bin(const char *name, const void *buf, unsigned size) {
	FILE *fo = fopen(name, "wb");
	if (!fo) fprintf(stderr, "fopen(output) failed\n");
	else {
		fwrite(buf, 1, size, fo);
		fclose(fo);
	}
}

static int check_lcd_entry(uint32_t *p) {
	if (p[6] != 9) return 1;
	if (p[1] | p[2] | p[7] | p[8] | p[9] | p[10]) return 1;
	return 0;
}

static void find_lcd_list(uint8_t *buf, unsigned size, uint32_t offset) {
	unsigned i, size_req = 0x2c;
	if (size < size_req) return;
	size &= ~3;
	// printf("!!! find_lcd_list\n");
	for (i = 0; i < size - size_req + 1; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);
		do {
			uint32_t a, *p2 = p;
			if (p[6] != 9) break;
			if (p[1] | p[2] | p[7] | p[8] | p[9] | p[10]) break;
			a = p[5];
			if (a < 0x1000 || (a & ~0x043ffffc)) break;
			printf("0x%x: LCD, id = 0x%06x (%u, %u, %u), addr = 0x%x\n",
					(int)((uint8_t*)p2 - buf) + offset,
					p2[0], p2[3] & 0xffff, p2[3] >> 16, p2[4], p2[5]);
		} while (0);
	}
}

static void scan_init_seg(uint8_t *buf, unsigned size, uint32_t offset) {
	unsigned i, size_req = 0x14 + 8;
	if (size < size_req) return;
	size &= ~3;
	for (i = 0; i < size - size_req + 1; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);
		// The structure itself is not very useful,
		// but it is followed by a list of supported LCDs.
		do {
			unsigned n, size2, j, k; uint32_t *p2;
			if (p[0] != 0x28) break;
			n = p[3];
			if ((n - 1) >> 4) break;
			j = n * 0x24;
			if (p[2] - p[4] != j) break;
			j += 8;
			if (i < j) break;
			if (p[4] != offset + i - j) break;
			if (p[-1] != 100000) break;
			size2 = size - i;
			if (size2 < 0x14) break;
			size2 -= 0x14;
			p2 = (uint32_t*)((uint8_t*)p + 0x14);
			printf("0x%x (0x%x + 0x%x): fat_config\n", offset + i, offset, i);
			clues.keymap_addr = p[1] + 0xc;
			printf("guess: keymap addr = 0x%x\n", p[1] + 0xc);

			for (k = 0x2c, j = 0; size2 >= k; j++) {
				size2 -= k;
				if (check_lcd_entry(p2)) {
					// special case:
					// has an extra field, which is the name of the LCD
					if (j != 1 || size2 < 8 || check_lcd_entry(++p2)) break;
					size -= 8; k += 4;
				}
				printf("0x%x: LCD, id = 0x%06x (%u, %u, %u), addr = 0x%x\n",
						(int)((uint8_t*)p2 - buf) + offset,
						p2[0], p2[3] & 0xffff, p2[3] >> 16, p2[4], p2[5]);
				p2 = (uint32_t*)((uint8_t*)p2 + k);
			}
			if (!j) find_lcd_list(buf, size, offset);
			return;
		} while (0);
	}
}

static void find_tfboot(uint8_t *buf, uint32_t size, unsigned offset) {
	unsigned i, j, size2 = size - offset;
	if (clues.tfboot_offs) return;
	if (size2 < 0x1000) return;
	size2 = offset + 0x400;
	for (i = offset; i < size2; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);
		for (j = 0; j < 15; j++)
			if (p[j] != 0xe3a00000 + (j << 12)) break;
		if (j < 15) continue;
		if (p[0xb0 / 4] != 0x4c4d5053) continue;
		buf += i; size -= i;
		clues.tfboot_offs = i;
		clues.tfboot_size = size;
		// TODO: find the original size
		printf("0x%x: internal tfboot (size <= 0x%x)\n", i, size);
		save_bin("tfboot.bin", buf, size);
		return;
	}
}

static uint32_t print_init_table(uint8_t *buf, unsigned size, uint32_t o1, int flags) {
	uint32_t *p = (uint32_t*)(buf + o1), *p2;
	uint32_t o2, o3, i, n, size2;
	uint32_t lz_addr = 0, lz_type = -1;
	uint32_t copy_addr = 0, zero_addr = 0, nop_addr;
	uint32_t fwaddr = 0, found_fwaddr = 0;

	size2 = size - o1 - 8;
	o2 = *p++; o3 = *p++;
	if (o3 > (size - o1)) return 0;
	o2 += o1; o3 += o1;
	if (o2 - clues.tfboot_offs < clues.tfboot_size) return 0;
	printf("0x%x: init_table, start = 0x%x, end = 0x%x\n", o1, o2, o3);
	nop_addr = o1 - 0x1e;

	if (size2 >= 0x10) do {
		unsigned n = 0;
		if (p[0] == 0xf04f440a && p[1] == 0xf8100c00 &&
				p[2] == 0xf0133b01 && p[3] == 0xbf080407)
			lz_type = 3, n = 0x5c;
		else if (p[0] == 0x3b01f810 && p[1] == 0xf013440a &&
				p[2] == 0xbf080403 && p[3] == 0x4b01f810)
			lz_type = 2, n = 0x64;
		else break;
		lz_addr = (uint8_t*)p - buf;
		if (n) printf("0x%x: init_lzdec%u\n", lz_addr, lz_type);
		if (size2 < n) return 0;
		size2 -= n; p += n >> 2;
	} while (0);

	if (size2 >= 0x1c && p[0] == 0xbf243a10) {
		unsigned n = 0x1c;
		copy_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("0x%x: init_copy\n", copy_addr);
	}
	if (size2 >= 0x1c && p[0] == 0x24002300) {
		unsigned n = 0x1c;
		zero_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("0x%x: init_zero\n", zero_addr);
	}

	n = o3 - o2;
	if (o2 >= o3 || (n & 0xf) || ((n >> 4) - 1) >= 20) return 0;
	p = (uint32_t*)(buf + o2);
	n >>= 4;

	{
		uint32_t buf[4] = { lz_addr, copy_addr, zero_addr, nop_addr };
		unsigned j, k, x = 0;
		for (j = 0; j < 4; j++) {
			uint32_t a = buf[j];
			if (!a) continue;
			a = p[3] - a;
			for (i = 0; i < n; i++) {
				for (k = 0; k < 4; k++)
					if (buf[k] && p[i * 4 + 3] - a == buf[k]) break;
				if (k == 4) break;
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
		if (lz_addr) lz_addr += fwaddr;
		if (copy_addr) copy_addr += fwaddr;
		if (zero_addr) zero_addr += fwaddr;
		for (p2 = p, i = 0; i < n; i++, p2 += 4) {
			uint32_t a = p2[0];
			if (zero_addr && p2[3] == zero_addr) continue;
			if (nop_addr && p2[3] == nop_addr) continue;
			if (a < fwaddr) continue;
			a -= fwaddr;
			if (ps_size > a) ps_size = a;
		}
		if (!clues.ps_addr)
			clues.ps_addr = fwaddr;
		printf("ps_addr: 0x%x\n", fwaddr);
		printf("ps_size: 0x%x\n", ps_size);
		if (flags & 2) {
			static unsigned init_count = 0;
			FILE *fo = NULL; uint32_t next = 0;
			size_t (*lzdec_fn)(const uint8_t*, size_t*, uint8_t*, size_t);
			lzdec_fn = NULL;
			if (lz_type == 2) lzdec_fn = &sprd_lzdec2;
			if (lz_type == 3) lzdec_fn = &sprd_lzdec3;

			for (p2 = p, i = 0; i < n; i++, p2 += 4) {
				uint32_t offs = p2[0], size2;
				if (offs < fwaddr) continue;
				offs -= fwaddr;
				if (offs >= size) continue;

				if (!(copy_addr && p2[3] == copy_addr) &&
						!(lzdec_fn && p2[3] == lz_addr))
					continue;

				if ((flags & 1) && (!fo || p2[1] != next)) {
					char name[64];
					if (fo) fclose(fo);
					if (init_count)
						snprintf(name, sizeof(name), "init%u_%08x.bin", init_count, p2[1]);
					else
						snprintf(name, sizeof(name), "init_%08x.bin", p2[1]);
					fo = fopen(name, "wb");
					if (!fo)
						fprintf(stderr, "fopen(output) failed\n");
				}
				next = 0;
				size2 = size - offs;
				if (copy_addr && p2[3] == copy_addr) {
					if (size2 > p2[2]) size2 = p2[2];
					scan_init_seg(buf + offs, size2, p2[1]);
					if (fo) next = fwrite(buf + offs, 1, size2, fo);
				} else if (lzdec_fn && p2[3] == lz_addr) {
					uint8_t *mem = malloc(p2[2]);
					if (mem) {
						size_t src_size = size2;
						size2 = lzdec_fn(buf + offs, &src_size, mem, p2[2]);
						scan_init_seg(mem, size2, p2[1]);
						if (fo) next = fwrite(mem, 1, size2, fo);
						free(mem);
					}
				}
				next += p2[1];
			}
			if (fo) fclose(fo);
			init_count++;
		}
		find_tfboot(buf, ps_size, o3);
		return ps_size;
	}
	return 0;
}

static int check_keymap(const void *buf, unsigned size) {
	const uint16_t *s = (const uint16_t*)buf;
	unsigned a, i, n, empty = 0, empty2 = 0;
	n = size >> 1;
	if (n < 40) return 0;
	a = 64;
	if (n >= a) n = a;
	for (i = 0; i < n; i++) {
		a = s[i];
		if (a == 0xffff) { empty++; empty2 += (i & 7) >= 6; continue; }
		if (a - 1 >= 0x39) break;
	}
	// printf("!!! check_keymap: %d, %d, %d\n", i, empty, empty2);
	if (i < 40 || empty < i - 32) return 0;
	if (i > 40 && empty2 != i >> 2) return 0;
	return i << 1;
}

#define KEYPAD_ENUM(M) \
	M(0x01, DIAL) \
	M(0x04, UP) M(0x05, DOWN) M(0x06, LEFT) M(0x07, RIGHT) \
	M(0x08, LSOFT) M(0x09, RSOFT) M(0x0d, CENTER) \
	M(0x0e, CAMERA) M(0x1d, EXT_1D) M(0x1f, EXT_1F) \
	M(0x21, EXT_21) M(0x23, HASH) M(0x24, VOLUP) M(0x25, VOLDOWN) \
	M(0x29, EXT_29) M(0x2a, STAR) M(0x2b, PLUS) \
	M(0x2c, EXT_2C) M(0x2d, MINUS) M(0x2f, EXT_2F) \
	M(0x30, 0) M(0x31, 1) M(0x32, 2) M(0x33, 3) M(0x34, 4) \
	M(0x35, 5) M(0x36, 6) M(0x37, 7) M(0x38, 8) M(0x39, 9)

#define X(num, name) num,
static const uint16_t keypad_ids[] = { KEYPAD_ENUM(X) -1 };
#undef X
#define X(num, name) { #name },
static const char keypad_names[][8] = { KEYPAD_ENUM(X) };
#undef X

static const char* keypad_getname(unsigned a) {
	unsigned i;
	for (i = 0; keypad_ids[i] != 0xffff; i++)
		if (keypad_ids[i] == a) return keypad_names[i];
	return NULL;
}

static void check_keymap2(uint8_t *buf, unsigned size, uint32_t addr, int flags) {
	if (!clues.keymap_addr) return;
	addr = clues.keymap_addr - addr;
	if (size <= addr) return;
	size = check_keymap(buf + addr, size - addr);
	if (!size) return;

	printf("0x%x: keymap", addr);
	clues.keymap_addr = 0;
	{
		uint16_t *p = (uint16_t*)(buf + addr);
		unsigned a, i, nrow = 8; const char *name;
		if ((a = *p) != 0xffff) {
			printf(", bootkey = 0x%02x", a);
			name = keypad_getname(a);
			if (name) printf(" (%s)", name);
		}
		printf(", bl_update keys = 0x%02x 0x%02x (", p[1], p[nrow]);
		for (i = 1; i < 3; i++) {
			a = p[(i & 1) + (i >> 1) * nrow];
			name = "---";
			if (a != 0xffff) name = keypad_getname(a);
			printf("%s%s", name ? name : "???", i < 2 ? ", " : ")");
		}
	}
	printf("\n");

	if (flags & 1)
		save_bin("keymap.bin", buf + addr, size);
}

static void scan_fw(uint8_t *buf, unsigned size, int flags) {
	unsigned i = 0, size_req = 0x1c;
	unsigned size2, ps_size = 0;
	size &= ~3;
	if (size < size_req) return;
	ps_size = size;

	do {
		uint32_t *p = (uint32_t*)buf;
		if (p[0] != 0x42544844) break;
		if (p[1] != 1) break;
		printf("0x%x: DHTB header (size = 0x%x)\n", i, p[0xc]);
		i = 0x200 / 4;
	} while (0);

	for (; i < size - size_req + 1; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);

		do {
			if (p[0] != 0xe8ba0e09) break;
			if (p[1] != 0xf013000f) break;
			if (p[2] != 0xbf180f01) break;
			if (p[3] != 0xf0431afb) break;
			if (p[4] != 0x47180301) break;
			{
				uint32_t a = print_init_table(buf, size, i + 0x14, flags);
				if (a && ps_size == size) ps_size = a;
			}
		} while (0);

#if 0
		do {
			if (p[0] != 0xe6) break;
			if (p[1] != 0x400) break;
			if (p[2] != 0x0a) break;
			// keymap = p + 3
		} while (0);
#endif

		do {
			uint32_t *p2 = p + 6, a;
			if ((p[0] ^ 0x402a0000) >> 12) break;
			if ((p[2] ^ 0x402a0000) >> 12) break;
			if ((p[4] ^ 0x402a0000) >> 12) break;
			size2 = ((size - i) >> 3) - 3;
			for (; size2--; p2 += 2) {
				if ((a = p2[0]) == ~0u && p2[1] == ~0u) {
					unsigned end = (uint8_t*)(p2 + 2) - buf;
					printf("0x%x: pinmap (end = 0x%x)\n", i, end);
					if (!clues.pinmap_offs) {
						clues.pinmap_offs = i;
						if (flags & 1)
							save_bin("pinmap.bin", buf + i, end - i);
					}
					i = end - 4;
					break;
				}
				if ((a ^ 0x402a0000) >> 12 && (a ^ 0x40608000) >> 12) break;
			}
		} while (0);
	}

	if (ps_size < size)
		check_keymap2(buf, ps_size, clues.ps_addr, flags);
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

static void lcd_init_print(int cmd, int ndata, uint8_t *data) {
	int i;
	if (cmd < 0) {
		if (!ndata) return;
		printf("LCM_DATA(%u),", ndata);
	} else {
		printf("LCM_CMD(0x%02x, %u),", cmd, ndata);
	}
	if (ndata) {
		printf(" ");
		for (i = 0; i < ndata; i++)
			printf("0x%02x,", data[i]);
	}
	printf("\n");
}

// decompiles LCD init code
static void lcd_init_dec(uint8_t *buf, unsigned size, unsigned pos, unsigned mode) {
	int reg[8], flags = 0;
	uint16_t *p, *end;
	int badptr = -1, lcm_wait = badptr, lcm_cmd = badptr, lcm_data = badptr;
	int last_cmd = -1, ndata = 0;
	uint8_t data[31];

	if (mode >= 2) { printf("!!! unknown mode\n"); return; }
	if (pos >= size) { printf("!!! pos >= size\n"); return; }
	pos &= ~1;
	p = (uint16_t*)(buf + pos);
	end = (uint16_t*)&buf[size & ~1];
#define CUR_POS (int)((uint8_t*)p - buf)
	for (;;) {
		int a, b, c;
		if (p == end) break;
		a = *p++;
		// MOV Rd, imm8
		if ((a & 0xf800) == 0x2000) {
			b = a >> 8 & 7;
			if (flags >> b & 1) break;
			reg[b] = a & 0xff;
			flags |= 1 << b;
		// MOVS/MOV Rd, Rm
		} else if (!(a & 0xffc0) || (a & 0xffc0) == 0x4600) {
			b = a & 7;
			c = a >> 3 & 7;
			if (!(flags >> c & 1)) break;
			reg[b] = reg[c];
			flags |= 1 << b;
		// BL/BLX
		} else if ((a & 0xf800) == 0xf000) {
			b = (int32_t)(a << 21) >> (21 - 12);
			if (p == end) break;
			a = *p++;
			b |= (a & 0x7ff) << 1;
			// thumb2 extra
			b ^= (((~a & 0x2800) + 0x800) & 0x3000) << 10;
			if ((a & 0xd001) == 0xc000) b &= ~3; // BLX
			else if (!(a & 0x1000)) break; // !BL
			b += (uint8_t*)p - buf;
			c = flags & 15;
			if (c & (c + 1)) {
				printf("!!! 0x%x: unexpected call args\n", CUR_POS - 4);
				break;
			}
			// detect shortened calls
			if ((a & 0x1000) && c == 1) { // BL
				if (size > (unsigned)b && (size - b) >= 6) {
					uint16_t *p2 = (uint16_t*)(buf + b);
					if (*p2 == 0x2100 && (p2[1] & 0xf800) == 0xf000)
						reg[1] = 0, c = 3;
				}
			}
			if (mode == 1) {
				if (c == 1) {
					if (lcm_wait == badptr) lcm_wait = b;
					if (b != lcm_wait) {
						printf("!!! 0x%x: unexpected call\n", CUR_POS - 4);
						break;
					}
					lcd_init_print(last_cmd, ndata, data);
					ndata = 0; last_cmd = -1;
					printf("LCM_DELAY(%u),\n", reg[0]);
				} else if (c == 3) {
					if (reg[1] != 0) {
						printf("!!! 0x%x: r1 != 0\n", CUR_POS - 4);
						break;
					}
					if (lcm_cmd == badptr) lcm_cmd = b;
					if (b == lcm_cmd) {
						if (last_cmd >= 0 || ndata) {
							lcd_init_print(last_cmd, ndata, data);
							ndata = 0;
						}
						last_cmd = reg[0];
					} else {
						if (lcm_data == badptr) lcm_data = b;
						if (b == lcm_data) {
							if (ndata == sizeof(data)) {
								lcd_init_print(last_cmd, ndata, data);
								ndata = 0; last_cmd = -1;
							}
							data[ndata++] = reg[0];
						} else {
							printf("!!! 0x%x: unexpected call\n", CUR_POS - 4);
							break;
						}
					}
				} else {
					printf("!!! 0x%x: unexpected call\n", CUR_POS - 4);
					break;
				}
			} else {
				printf("bl%s_%x(", "x" + (a >> 12 & 1), b);
				for (b = 0; c >> b & 1; b++)
					printf("%s0x%02x", b ? ", " : "", reg[b]);
				printf(")\n");
			}
			flags &= ~15;
		// B
		} else if ((a & 0xf800) == 0xe000) {
			a = (int32_t)(a << 21) >> 21;
			if (a < 0) break;
			a++;
			if (end - p < a) {
				printf("!!! jump out of the buffer\n");
				break;
			}
			p += a;
		} else {
			printf("!!! 0x%x: unknown op 0x%04x\n", CUR_POS - 2, a);
			break;
		}
	}
#undef CUR_POS
	if (mode == 1) {
		lcd_init_print(last_cmd, ndata, data);
		printf("LCM_END\n");
	}
}

int main(int argc, char **argv) {
	uint8_t *mem; size_t size = 0;

	if (argc < 3)
		ERR_EXIT("Usage: %s flash.bin cmd args...\n", argv[0]);

	mem = loadfile(argv[1], &size);
	if (!mem) ERR_EXIT("loadfile failed\n");
	argc -= 1; argv += 1;

	while (argc > 1) {
		if (!strcmp(argv[1], "scan")) {
			scan_fw(mem, size, 2);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "unpack")) {
			scan_fw(mem, size, 2 + 1);
			argc -= 1; argv += 1;
		} else if (!strcmp(argv[1], "lcd_init_dec")) {
			unsigned pos;
			if (argc <= 3) ERR_EXIT("bad command\n");
			pos = strtoul(argv[2], NULL, 0);
			lcd_init_dec(mem, size, pos, atoi(argv[3]));
			argc -= 3; argv += 3;
		} else if (!strcmp(argv[1], "copy")) {
			if (run_decoder(mem, size, argc, argv, &decode_copy)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "lzdec2")) {
			if (run_decoder(mem, size, argc, argv, &sprd_lzdec2)) return 1;
			argc -= 4; argv += 4;
		} else if (!strcmp(argv[1], "lzdec3")) {
			if (run_decoder(mem, size, argc, argv, &sprd_lzdec3)) return 1;
			argc -= 4; argv += 4;
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

