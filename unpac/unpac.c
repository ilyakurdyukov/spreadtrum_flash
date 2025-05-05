#define _GNU_SOURCE 1
#define _FILE_OFFSET_BITS 64

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

typedef struct {
	uint16_t pac_version[24];
	uint32_t pac_size;
	uint16_t fw_name[256];
	uint16_t fw_version[256];
	uint32_t file_count;
	uint32_t dir_offset;
	uint32_t unknown1[5];
	uint16_t fw_alias[100];
	uint32_t unknown2[3];
  uint32_t unknown[200];
	uint32_t pac_magic;
	uint16_t head_crc, data_crc;
} sprd_head_t;

typedef struct {
  uint32_t struct_size;
	uint16_t id[256];
	uint16_t name[256];
	uint16_t unknown1[256 - 4];
	uint32_t size_high;
	uint32_t pac_offset_high;
	uint32_t size;
	uint32_t type; // 0 - operation, 1 - file, 2 - xml, 0x101 - fdl
	uint32_t flash_use; // 1 - used during flashing process							
	uint32_t pac_offset;
	uint32_t omit_flag;
	uint32_t addr_num;
	uint32_t addr[5];
	uint32_t unknown2[249];
} sprd_file_t;

static unsigned crc16(uint32_t crc, const void *src, unsigned len) {
	uint8_t *s = (uint8_t*)src; int i;
	while (len--) {
		crc ^= *s++;
		for (i = 0; i < 8; i++)
			crc = crc >> 1 ^ ((0 - (crc & 1)) & 0xa001);
	}
  return crc;
}

#define ERR_EXIT(...) \
	do { fprintf(stderr, __VA_ARGS__); exit(1); } while (0)

#define READ(p, n, name) \
	if (fread(p, n, 1, fi) != 1) \
		ERR_EXIT("fread(%s) failed\n", #name)
#define READ1(p) READ(&p, sizeof(p), #p)

enum {
	MODE_NONE = 0,
	MODE_LIST,
	MODE_EXTRACT,
	MODE_CHECK
};

static size_t u16_to_u8(char *d, size_t dn, const uint16_t *s, size_t sn) {
	size_t i = 0, j = 0; unsigned a;
	if (!d) dn = 0;
	while (i < sn) {
		a = s[i++];
		if (!a) break;
		if ((a - 0x20) >= 0x5f) a = '?';
		if (j + 1 < dn) d[j++] = a;
	}
	if (dn) d[j] = 0;
	return i;
}

static int compare_u8_u16(int depth, char *d, const uint16_t *s, size_t sn) {
	size_t i = 0; int a, b;
	if (depth > 10) ERR_EXIT("use less wildcards\n");
	for (;;) {
		a = *d++;
		if (a == '*') goto wildcard;
		b = i < sn ? s[i++] : 0;
		if (a == '?') {
			if (!b) return 1;
		} else {
			if (a != b) return 1;
			if (!a) break;
		}
	}
	return 0;

wildcard:
	for (;;) {
		if (!compare_u8_u16(depth + 1, d, s + i, sn - i)) return 0;
		b = i < sn ? s[i++] : 0;
		if (!b) break;
	}
	return 1;
}

static int check_path(char *path) {
	char *s = path; int a;
	for (; (a = *s); s++) {
		if (a == '/' || a == '\\' || a == ':') return -1;
	}
	return s - path;
}

int main(int argc, char **argv) {
	FILE *fi; sprd_head_t head;
	char str_buf[257];
	unsigned i, mode, chunk = 0x1000;
	const char *dir = NULL;
	uint8_t *buf = NULL;

	while (argc > 1 && *argv[1] == '-') {
		if (!strcmp(argv[1], "-d") || !strcmp(argv[1], "--dir")) {
			dir = argv[2]; argv += 2; argc -= 2;
		} else if (!strcmp(argv[1], "--")) {
			argv++; argc--; break;
		} else ERR_EXIT("unknown option\n");
	}

	if (argc < 3)
		ERR_EXIT("Usage: unpac [-d dir] {list|extract|check} firmware.pac [names]\n");

	if (!strcmp(argv[1], "list")) {
		mode = MODE_LIST;
	} else if (!strcmp(argv[1], "extract")) {
		mode = MODE_EXTRACT;
	} else if (!strcmp(argv[1], "check")) {
		mode = MODE_CHECK;
		if (argc > 3) ERR_EXIT("extra arguments\n");
	} else {
		ERR_EXIT("unknown mode\n");
	}

	fi = fopen(argv[2], "rb");
	if (!fi) ERR_EXIT("fopen(input) failed\n");
	argv += 3; argc -= 3;

	if (mode == MODE_EXTRACT && dir && chdir(dir))
		ERR_EXIT("chdir failed\n");

	READ1(head);
	if (head.pac_magic != ~0x50005u)
		ERR_EXIT("bad pac_magic\n");

#define CONV_STR(x) \
	u16_to_u8(str_buf, sizeof(str_buf), x, sizeof(x) / 2)

	if (mode == MODE_LIST) {
		CONV_STR(head.pac_version);
		printf("pac_version: %s\n", str_buf);
		printf("pac_size: %u\n", head.pac_size);

		CONV_STR(head.fw_name);
		printf("fw_name: %s\n", str_buf);
		CONV_STR(head.fw_version);
		printf("fw_version: %s\n", str_buf);
		CONV_STR(head.fw_alias);
		printf("fw_alias: %s\n", str_buf);
	}

	if (mode == MODE_LIST || mode == MODE_CHECK) {
		uint32_t head_crc = crc16(0, &head, sizeof(head) - 4);
		printf("head_crc: 0x%04x", head.head_crc);
		if (head.head_crc != head_crc)
			printf(" (expected 0x%04x)", head_crc);
		printf("\n");
	}

	if (head.dir_offset != sizeof(head))
		ERR_EXIT("unexpected directory offset\n");

	if (head.file_count >> 10)
		ERR_EXIT("too many files\n");

	if (mode == MODE_LIST || mode == MODE_EXTRACT)
	for (i = 0; i < head.file_count; i++) {
		sprd_file_t file; int j;
		long long file_size, pac_offset;
		READ1(file);
		if (file.struct_size != sizeof(sprd_file_t))
			ERR_EXIT("unexpected struct size\n");

		file_size = (long long)file.size_high << 32 | file.size;
		pac_offset = (long long)file.pac_offset_high << 32 | file.pac_offset;
		if (mode == MODE_EXTRACT)
			if (!file.name[0] || !pac_offset || !file_size) continue;

		for (j = 0; j < argc; j++)
			if (!compare_u8_u16(0, argv[j], file.name, 256) || 
					(file.id[0] && !compare_u8_u16(0, argv[j], file.id, 256)))
				break;

		if (argc && j == argc) continue;

		if (mode == MODE_LIST) {
			printf(file.type > 9 ? "type = 0x%x" : "type = %u", file.type);
			if (file_size)
				printf(", size = 0x%llx", file_size);
			if (pac_offset)
				printf(", offset = 0x%llx", pac_offset);

			if (file.addr_num <= 5)
				for (j = 0; j < (int)file.addr_num; j++) {
					if (!file.addr[j]) continue;
					if (!j) printf(", addr = 0x%x", file.addr[j]);
					else printf(", addr%u = 0x%x", j, file.addr[j]);
				}

			if (file.id[0]) {
				CONV_STR(file.id);
				printf(", id = \"%s\"", str_buf);
			}
			if (file.name[0]) {
				CONV_STR(file.name);
				printf(", name = \"%s\"", str_buf);
			}
			printf("\n");
		} else {
			FILE *fo; uint64_t l; uint32_t n;

			CONV_STR(file.name);
			printf("%s\n", str_buf);

			if (fseeko(fi, pac_offset, SEEK_SET))
				ERR_EXIT("fseek failed\n");

			if (check_path(str_buf) < 1) {
				printf("!!! unsafe filename detected\n");
				continue;
			}

			if (!buf) {
				buf = malloc(chunk);
				if (!buf) ERR_EXIT("malloc failed\n");
			}

			fo = fopen(str_buf, "wb");
			if (!fo)
				ERR_EXIT("fopen(output) failed\n");

			l = file_size;
			for (; l; l -= n) {
				n = l > chunk ? chunk : l;
				READ(buf, n, "chunk");
				fwrite(buf, n, 1, fo);
			}
			fclose(fo);

			if (fseek(fi, sizeof(head) + (i + 1) * sizeof(sprd_file_t), SEEK_SET))
				ERR_EXIT("fseek failed\n");
		}
	}
	else if (mode == MODE_CHECK) {
		uint32_t n, l = head.pac_size;
		uint32_t data_crc = 0;
		buf = malloc(chunk);
		if (!buf) ERR_EXIT("malloc failed\n");
		n = sizeof(head);
		if (l < n) ERR_EXIT("unexpected pac size\n");
		for (l -= n; l; l -= n) {
			n = l > chunk ? chunk : l;
			READ(buf, n, "chunk");
			data_crc = crc16(data_crc, buf, n);
		}
		printf("data_crc: 0x%04x", head.data_crc);
		if (head.data_crc != data_crc)
			printf(" (expected 0x%04x)", data_crc);
		printf("\n");
	}
	if (buf) free(buf);
	fclose(fi);
	return 0;
}
