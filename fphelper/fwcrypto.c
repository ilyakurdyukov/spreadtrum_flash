#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/sha.h>

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

	rsa = rsa_from_file(priv_fn, 1);
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
		} else if (!strcmp(argv[1], "sign_fw")) {
			uint32_t size;
			if (argc <= 2) ERR_EXIT("bad command\n");
			size = strtol(argv[4], NULL, 0); // -1 = don't change the size
			// $ ./fwcrypto sign_fw dump.bin key.pem fw_size signed.bin
			sign_fw(argv[2], argv[3], size, argv[5]);
/*
// $ dd if=signed.bin bs=1 count=136 skip=66052 > patch.bin
// $ ./spd_dump fdl nor_fdl1.bin 0x40004000 \
//           write_data fw+0x10204 0 0 patch.bin
*/
			argv += 5; argc -= 5;
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

