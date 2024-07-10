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

static void print_init_table(uint8_t *buf, unsigned size, uint32_t o1) {
	uint32_t *p = (uint32_t*)(buf + o1);
	uint32_t o2, o3, i, n, size2;
	uint32_t lz_addr = 0, lz_type = -1;
	uint32_t copy_addr = 0, zero_addr = 0;

	size2 = size - o1 - 8;
	o2 = *p++; o3 = *p++;
	if (o3 > (size - o1)) return;
	o2 += o1; o3 += o1;
	printf("init_table: ref = 0x%x, start = 0x%x, end = 0x%x\n", o1, o2, o3);

	if (size2 >= 0x10 && p[0] == 0xe28fc001) {
		unsigned n = 0;
		lz_addr = (uint8_t*)p - buf;
		if (p[3] == 0x075c3001) lz_type = 3, n = 0x5c;
		if (p[3] == 0x079c3001) lz_type = 2, n = 0x60;
		if (n) printf("init_lzdec%u: 0x%x\n", lz_type, lz_addr);
		if (size2 < n) return;
		size2 -= n; p += n >> 2;
	}
	if (size2 >= 0x28 && p[0] == 0xe2522010) {
		unsigned n = 0x28;
		copy_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("init_copy: 0x%x\n", copy_addr);
	}
	if (size2 >= 0x38 && p[0] == 0xe3b03000) {
		unsigned n = 0x38;
		zero_addr = (uint8_t*)p - buf;
		size2 -= n; p += n >> 2;
		printf("init_zero: 0x%x\n", zero_addr);
	}

	n = o3 - o2;
	if (o2 >= o3 || (n & 0xf) || ((n >> 4) - 1) >= 20) return;
	p = (uint32_t*)(buf + o2);

	for (i = 0; i < (n >> 4); i++, p += 4)
		printf("%u: src = 0x%x, dst = 0x%08x, len = 0x%x, fn = 0x%x\n",
				i, p[0], p[1], p[2], p[3]);
	printf("\n");
}

static void scan_fw(uint8_t *buf, unsigned size) {
	unsigned i, size_req = 0x1c;
	size &= ~3;
	if (size > size_req)
	for (i = 0; i < size - size_req; i += 4) {
		uint32_t *p = (uint32_t*)(buf + i);
		do {
			if (p[0] != 0xe8ba000f) break;
			if (p[1] != 0xe24fe018) break;
			if (p[2] != 0xe3130001) break;
			if (p[3] != 0x1047f003) break;
			if (p[4] != 0xe12fff13) break;
			print_init_table(buf, size, i + 0x14);
		} while (0);

		do {
			size_t size2 = ((size - i) >> 3) - 3;
			uint32_t *p2 = p + 6, a;
			if ((p[0] ^ 0x8c000000) >> 12) break;
			if ((p[2] ^ 0x8c000000) >> 12) break;
			if ((p[4] ^ 0x8c000000) >> 12) break;
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
	if (!fo) ERR_EXIT("fopen(output) failed\n");
	else {
		fwrite(dst, 1, result, fo);
		fclose(fo);
	}
	return 0;
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
			scan_fw(mem, size);
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
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

