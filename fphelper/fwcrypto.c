#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

static void sprd_sha1(const uint8_t *buf, size_t len, uint8_t *hash) {
	size_t i; const uint8_t *p = buf;
	SHA_CTX ctx;
	SHA1_Init(&ctx);
	for (i = 0; i < (len & ~3); i += 4, p += 4) {
		uint8_t temp[4] = { p[3], p[2], p[1], p[0] };
		SHA1_Update(&ctx, temp, 4);
	}
	SHA1_Final(hash, &ctx);
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

static RSA *rsa_from_file(const char *rsa_fn, int priv) {
	RSA *rsa = NULL; BIO *bio;
	bio = BIO_new_file(rsa_fn, "r");
	if (!bio) printf("!!! BIO_new_file failed\n");
	else {
		if (priv)
			rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
		else
			rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);

		if (!rsa)	printf("!!! PEM_read_bio_RSA%s failed\n", priv ? "PrivateKey" : "_PUBKEY");
		BIO_free(bio);
	}
	return rsa;
}

#define WRITE32_BE(p, a) do { \
	((uint8_t*)(p))[0] = (a) >> 24; \
	((uint8_t*)(p))[1] = (a) >> 16; \
	((uint8_t*)(p))[2] = (a) >> 8; \
	((uint8_t*)(p))[3] = (uint8_t)(a); \
} while (0)

#define READ32_BE(p) ( \
	((uint8_t*)(p))[0] << 24 | \
	((uint8_t*)(p))[1] << 16 | \
	((uint8_t*)(p))[2] << 8 | \
	((uint8_t*)(p))[3])

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

#define ERR_EXIT(...) \
	do { fprintf(stderr, "!!! " __VA_ARGS__); exit(1); } while (0)

static void rand_init(uint8_t *seed, unsigned init) {
	int i, a = 0;
	// MD5("", 1, hash);
	static const uint8_t hash[16] = {
		0x93,0xb8,0x85,0xad,0xfe,0x0d,0xa0,0x89,
		0xcd,0xf6,0x34,0x90,0x4f,0xd5,0x9f,0x71 };
	for (i = 0; i < 16; i++) seed[i] = init, init >>= 1;
	for (i = 15; i >= 0; i--, a >>= 8)
		seed[i] = a += seed[i] + (hash[i] << 8);
}

static inline void rand_next(uint8_t *seed, uint8_t *buf) {
	unsigned i = 16;
	if (buf) MD5(seed, 16, buf);
	// seems to be a typo in the original code
	// should be preincrement
	while (!seed[--i]++ && i) (void)0;
}

static int fast_check_n(uint32_t hi) {
	unsigned i; uint8_t r[16], buf[20];
	for (i = 0; i < 1 << 23; i++) {
		uint32_t t0, t1;
		rand_init(r, i);
		rand_next(r, buf); // p
		rand_next(r, NULL);
		rand_next(r, NULL);
		rand_next(r, NULL);
		rand_next(r, buf + 4); // q
		t0 = READ32_BE(buf) | 0xc0000000;
		t1 = READ32_BE(buf + 4) | 0xc0000000;
		t0 = (uint64_t)t0 * t1 >> 32;
		if (hi - t0 < 2) return i;
	}
	return -1;
}

static int verify_fw(const char *bin_fn) {
	uint8_t *image, *boot_key, *vlr_head;
	size_t file_size; unsigned fw_size, boot_size;
	uint8_t data_hash[SHA_DIGEST_LENGTH];

	image = loadfile(bin_fn, &file_size);
	if (!image)
		ERR_EXIT("loadfile(\"%s\") failed\n", bin_fn);

	if (file_size < 0x10400 || memcmp(image + 0x20, "2656", 4))
		ERR_EXIT("unexpected header\n");

	boot_size = READ32_LE(image + 0x24);
	printf("boot size: 0x%x\n", boot_size * 4);
	if (boot_size >= 0x4000)
		ERR_EXIT("unexpected boot size\n");

	sprd_sha1(image, boot_size * 4, data_hash);

	{
		unsigned i;
		printf("boot hash:");
		for (i = 0; i < 20; i += 4)
			printf(" %08x",	READ32_LE(data_hash + i));
		printf("\n");
	}

	boot_key = image + 0x2dc;

	if (memcmp(image + 0x10000, "SPRD-SECUREFLAG", 15))
		ERR_EXIT("bad sign header\n");

	vlr_head = image + 0x10200;
	if (memcmp(vlr_head, "\xffVLR", 4))
		ERR_EXIT("bad vlr header\n");

	fw_size = READ32_LE(vlr_head + 0x88);
	printf("fw size: 0x%x\n", fw_size);
	if (fw_size > file_size - 0x10400)
		ERR_EXIT("unexpected fw size\n");

	sprd_sha1(image + 0x10400, fw_size, data_hash);

	{
		unsigned i;
		printf("fw hash:");
		for (i = 0; i < 20; i += 4)
			printf(" %08x",	READ32_LE(data_hash + i));
		printf("\n");
	}

	{
		BIGNUM *rsa_n, *rsa_e; RSA *rsa;
		uint8_t dec_buf[0x80];
		int padding = RSA_NO_PADDING;
		int key_bits = 1024;
		int dec_len;

		printf("high bits of N: 0x%08x\n", READ32_BE(boot_key + 4));

		rsa_n = BN_bin2bn(boot_key + 4, key_bits >> 3, NULL);
		rsa_e = BN_bin2bn(boot_key, 4, NULL);
		if (!rsa_n || !rsa_e) ERR_EXIT("BN_bin2bn failed\n");
		rsa = RSA_new();
		if (!rsa) ERR_EXIT("RSA_new failed\n");
		RSA_set0_key(rsa, rsa_n, rsa_e, NULL);

		dec_len = RSA_public_decrypt(key_bits >> 3, vlr_head + 4, dec_buf, rsa, padding);
		if (dec_len != 128)
			ERR_EXIT("wrong dec_len (%d, expected 128)\n", dec_len);
		if (memcmp(dec_buf + 128 - 20, data_hash, 20))
			printf("!!! decrypted data not match\n");

		if (1) {
			unsigned i;
			printf("decrypted data:\n");
			for (i = 0; i < 128; i += 16)
				printf("  %08x %08x %08x %08x\n",
						READ32_LE(dec_buf + i), READ32_LE(dec_buf + i + 4),
						READ32_LE(dec_buf + i + 8), READ32_LE(dec_buf + i + 12));
			printf("\n");
		}

		RSA_free(rsa);
	}
	return 0;
}

typedef uint32_t bignum_t;

static void bignum_frombin(bignum_t *d, uint8_t *s, unsigned n) {
	unsigned i;
	s += n;
	for (i = 0; i < n / 4; i++) s -= 4, *d++ = READ32_BE(s);
	if ((n &= 3)) {
		uint32_t a = s[-1];
		if (n > 1) {
			a |= s[-2] << 8;
			if (n > 2) a |= s[-3] << 16;
		}
		*d = a;
	}
}

static void bignum_tobin(uint8_t *d, bignum_t *s, unsigned n) {
	unsigned i;
	d += n * 4;
	for (i = 0; i < n; i++) { uint32_t a = *s++; d -= 4; WRITE32_BE(d, a); }
}

static void bignum_mul(bignum_t *d, bignum_t *s1, unsigned n1, bignum_t *s2, unsigned n2) {
	unsigned i, j;
	memset(d, 0, (n1 + n2) * sizeof(*d));
	for (i = 0; i < n1; i++, d++) {
		uint32_t m = *s1++, c = 0; uint64_t a, b;
		for (j = 0; j < n2; j++)
			b = (uint64_t)m * s2[j],
			d[j] = a = (uint64_t)d[j] + (uint32_t)b + c,
			c = (a >> 32) + (uint32_t)(b >> 32);
		while (c) a = (uint64_t)d[j] + c, d[j++] = a, c = a >> 32;
	}
}

static uint32_t rev_mul32(uint32_t v, uint32_t x) {
	uint32_t i = 1, a = 0;
	for (; i; i *= 2, v /= 2) if (v & 1) a |= i, v -= x;
	return a;
}

static RSA* create_rsa(BIGNUM *n, BIGNUM *e, BIGNUM *p, BIGNUM *q) {
	RSA *rsa = RSA_new();
	BN_CTX *ctx = BN_CTX_new();
	BIGNUM *d = BN_new(), *phi = BN_new();
	BIGNUM *p1 = BN_new(), *q1 = BN_new();

	if (!rsa) ERR_EXIT("RSA_new failed\n");

	// BN_mul(n, p, q, ctx);
	BN_sub(p1, p, BN_value_one()); // p-1
	BN_sub(q1, q, BN_value_one()); // q-1
	BN_mul(phi, p1, q1, ctx);
	BN_mod_inverse(d, e, phi, ctx);

	RSA_set0_key(rsa, n, e, d);
	RSA_set0_factors(rsa, p, q);
	{
		BIGNUM *dmp1 = BN_new(), *dmq1 = BN_new(), *iqmp = BN_new();
		BN_mod(dmp1, d, p1, ctx); // d mod (p-1)
		BN_mod(dmq1, d, q1, ctx); // d mod (q-1)
		BN_mod_inverse(iqmp, q, p, ctx);
		RSA_set0_crt_params(rsa, dmp1, dmq1, iqmp);
	}
	BN_free(phi); BN_free(p1); BN_free(q1);
	BN_CTX_free(ctx);
	return rsa;
}

static RSA* find_boot_key(uint8_t *boot_key) {
	uint8_t buf[128];
	bignum_t p[16], q[16], n[32], n2[32];
	uint32_t iter;
	RSA *rsa;

	bignum_frombin(n, boot_key + 4, 128);
	printf("boot key N: 0x%08x..%08x\n", n[31], n[0]);

	int seed = fast_check_n(READ32_BE(boot_key + 4));
	if (seed < 0) ERR_EXIT("key seed not found\n");
	printf("found key seed: 0x%06x\n", seed);

	{
		uint8_t r[16]; unsigned i;
		rand_init(r, seed);
		for (i = 0; i < 128; i += 16)
			rand_next(r, buf + i);
	}
	bignum_frombin(p, buf, 64);
	bignum_frombin(q, buf + 64, 64);

	p[0] |= 1; p[15] |= 3u << 30;
	q[0] |= 1; q[15] |= 3u << 30;

	for (iter = 0; iter < 1u << 31; iter++) {
		uint32_t p0 = p[0] ^ iter << 1;
		uint32_t q0 = rev_mul32(n[0], p0);
		uint32_t n1 = (uint64_t)p0 * q0 >> 32;
		n1 += p0 * q[1] + q0 * p[1];
		if (n1 == n[1]) {
			uint32_t old_p0 = p[0], old_q0 = q[0];
			printf("found p0/q0 match: 0x%08x 0x%08x\n", p0, q0);
			p[0] = p0; q[0] = q0;
			bignum_mul(n2, p, 16, q, 16);
			if (!memcmp(n2, n, sizeof(n))) goto found;
			p[0] = old_p0; q[0] = old_q0;
		}
	}
	ERR_EXIT("key not found\n");
found:
	{
		BIGNUM *rsa_n, *rsa_e, *rsa_p, *rsa_q;
		unsigned key_bits = 1024, swap;

		rsa_n = BN_bin2bn(boot_key + 4, 1024 >> 3, NULL);
		rsa_e = BN_bin2bn(boot_key, 4, NULL);

		bignum_tobin(buf, p, 16);
		bignum_tobin(buf + 64, q, 16);
		// P must be greater than Q
		swap = memcmp(p, q, 64) < 0 ? 64 : 0;
		rsa_p = BN_bin2bn(buf + swap, key_bits >> 4, NULL);
		rsa_q = BN_bin2bn(buf + (swap ^ 64), key_bits >> 4, NULL);

		rsa = create_rsa(rsa_n, rsa_e, rsa_p, rsa_q);
	}
	return rsa;
}

static int find_fw_key(const char *bin_fn, const char *priv_fn) {
	uint8_t *image, *boot_key;
	size_t file_size; unsigned boot_size;
	RSA *rsa; int ret;

	image = loadfile(bin_fn, &file_size);
	if (!image)
		ERR_EXIT("loadfile(\"%s\") failed\n", bin_fn);

	if (file_size < 0x10400 || memcmp(image + 0x20, "2656", 4))
		ERR_EXIT("unexpected header\n");

	boot_size = READ32_LE(image + 0x24);
	printf("boot size: 0x%x\n", boot_size * 4);
	if (boot_size >= 0x4000)
		ERR_EXIT("unexpected boot size\n");

	boot_key = image + 0x2dc;

	if (memcmp(image + 0x10000, "SPRD-SECUREFLAG", 15))
		ERR_EXIT("bad sign header\n");

	rsa = find_boot_key(boot_key);
	ret = RSA_check_key(rsa);
	if (ret != 1) ERR_EXIT("RSA_check_key failed\n");

	{
		BIO *bio = BIO_new_file(priv_fn, "wb");
		if (!bio) ERR_EXIT("BIO_new_file failed\n");
		ret = PEM_write_bio_RSAPrivateKey(bio, rsa, NULL, NULL, 0, NULL, NULL);
		if (ret != 1) ERR_EXIT("PEM_write_bio_RSAPrivateKey failed\n");
		BIO_free_all(bio);
	}

	RSA_free(rsa);
	return 0;
}

static int sign_fw(const char *bin_fn, const char *priv_fn,
		uint32_t new_fw_size, const char *out_fn) {
	uint8_t *image, *boot_key, *vlr_head;
	size_t file_size, fw_size;
	uint8_t data_hash[SHA_DIGEST_LENGTH];
	RSA *rsa;

	image = loadfile(bin_fn, &file_size);
	if (!image)
		ERR_EXIT("loadfile(\"%s\") failed\n", bin_fn);

	if (file_size < 0x10400 || memcmp(image + 0x20, "2656", 4))
		ERR_EXIT("unexpected header\n");

	boot_key = image + 0x2dc;

	if (memcmp(image + 0x10000, "SPRD-SECUREFLAG", 15))
		ERR_EXIT("bad sign header\n");

	vlr_head = image + 0x10200;
	if (memcmp(vlr_head, "\xffVLR", 4))
		ERR_EXIT("bad vlr header\n");

	fw_size = READ32_LE(vlr_head + 0x88);
	if (~new_fw_size) {
		fw_size = new_fw_size;
		WRITE32_LE(vlr_head + 0x88, fw_size);
	}
	if (fw_size > file_size - 0x10400)
		ERR_EXIT("bad fw size\n");

	sprd_sha1(image + 0x10400, fw_size, data_hash);

	if (*priv_fn) rsa = rsa_from_file(priv_fn, 1);
	else rsa = find_boot_key(boot_key);
	if (!rsa) exit(1);

	{
		const BIGNUM *rsa_n = NULL, *rsa_e = NULL;
		uint8_t buf[128];
		int ret, key_bits = 1024;
		int padding = RSA_NO_PADDING;

		RSA_get0_key(rsa, &rsa_n, &rsa_e, NULL);
		if (!rsa_n || !rsa_e) ERR_EXIT("RSA_get0_key failed\n");

		ret = BN_num_bytes(rsa_n) * 8;
		if (ret != key_bits)
			ERR_EXIT("wrong RSA key size (%d, expected 1024)\n", ret);

		if (BN_bn2binpad(rsa_e, buf, 4) < 0)
			ERR_EXIT("BN_bn2binpad failed\n");
		if (memcmp(buf, boot_key, 4)) ERR_EXIT("E not match\n");
		BN_bn2bin(rsa_n, buf);
		if (memcmp(buf, boot_key + 4, 128)) ERR_EXIT("N not match\n");

		ret = RSA_public_decrypt(key_bits >> 3, vlr_head + 4, buf, rsa, padding);
		if (ret != 128)
			ERR_EXIT("wrong dec_len (%d, expected 128)\n", ret);

		// update hash
		memcpy(buf + 128 - sizeof(data_hash), data_hash, sizeof(data_hash));

		ret = RSA_private_encrypt(key_bits >> 3, buf, vlr_head + 4, rsa, padding);
		if (ret != 128)
			ERR_EXIT("wrong enc_len (%d, expected 128)\n", ret);
	}

	RSA_free(rsa);

	{
		FILE *fo = fopen(out_fn, "wb");
		if (fo) {
			fwrite(image, 1, file_size, fo);
			fclose(fo);
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	while (argc > 1) {
		if (!strcmp(argv[1], "verify_fw")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			verify_fw(argv[2]);
			argv += 2; argc -= 2;
		} else if (!strcmp(argv[1], "find_fw_key")) {
			if (argc <= 3) ERR_EXIT("bad command\n");
			find_fw_key(argv[2], argv[3]);
			argv += 3; argc -= 3;
		} else if (!strcmp(argv[1], "sign_fw")) {
			uint32_t size;
			if (argc <= 5) ERR_EXIT("bad command\n");
			size = strtol(argv[4], NULL, 0); // -1 = don't change the size
			// $ ./fwcrypto sign_fw dump.bin key.pem fw_size signed.bin
			sign_fw(argv[2], argv[3], size, argv[5]);
/*
// $ dd if=signed.bin bs=1 count=136 skip=66052 > patch.bin
// $ ./spd_dump fdl nor_fdl1.bin 0x40004000 \
//           write_data fw+0x10204 0 0 patch.bin
*/
			argv += 5; argc -= 5;
		} else if (!strcmp(argv[1], "check_n")) {
			int ret;
			if (argc <= 2) ERR_EXIT("bad command\n");
			ret = fast_check_n(strtol(argv[2], NULL, 0));
			if (ret < 0) printf("key seed not found\n");
			else printf("found key seed: 0x%06x\n", ret);
			argv += 2; argc -= 2;
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

