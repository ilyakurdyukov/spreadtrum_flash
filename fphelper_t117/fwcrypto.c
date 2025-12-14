#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define OPENSSL_SUPPRESS_DEPRECATED
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <openssl/sha.h>

static void sha256(const uint8_t *buf, size_t len, uint8_t *hash) {
	SHA256_CTX ctx;
	SHA256_Init(&ctx);
	SHA256_Update(&ctx, buf, len);
	SHA256_Final(hash, &ctx);
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
#if 0
	uint8_t *mem; size_t size;

	mem = loadfile(rsa_fn, &size);
	if (!mem) {
		printf("!!! loadfile(\"%s\") failed\n", rsa_fn);
		return NULL;
	}

	bio = BIO_new_mem_buf(mem, size);
	if (!bio) printf("!!! BIO_new_mem_buf failed\n");
#else
	bio = BIO_new_file(rsa_fn, "r");
	if (!bio) printf("!!! BIO_new_file failed\n");
#endif
	else {
		if (priv)
			rsa = PEM_read_bio_RSAPrivateKey(bio, NULL, NULL, NULL);
		else
			rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);

		if (!rsa)	printf("!!! PEM_read_bio_RSA%s failed\n", priv ? "PrivateKey" : "_PUBKEY");
		BIO_free(bio);
	}
#if 0
	free(mem);
#endif
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

static int verify_image(const char *bin_fn) {
	uint8_t *image, *sign_head, *sign_data;
	size_t file_size, data_size, extra_size;
	uint8_t data_hash[SHA256_DIGEST_LENGTH];
	uint32_t sign_offs, sign_size, sign_type, tmp;
	uint32_t key_bits, key_size;

	image = loadfile(bin_fn, &file_size);
	if (!image)
		ERR_EXIT("loadfile(\"%s\") failed\n", bin_fn);
	if (file_size < 0x200 || memcmp(image, "DHTB", 4))
		ERR_EXIT("bad DHTB header\n");
	tmp = READ32_LE(image + 4);
	if (tmp != 1)
		ERR_EXIT("unknown DHTB version (%u)\n", tmp);
	extra_size = file_size - 0x200;
	data_size = READ32_LE(image + 0x30);
	if (extra_size < data_size)
		ERR_EXIT("bad data size\n");
	if (data_size & 3)
		ERR_EXIT("unaligned data size\n");

	extra_size -= data_size;
	if (!extra_size) 
		ERR_EXIT("no signature header\n");
	if (extra_size < 0x60)
		ERR_EXIT("too small signature header\n");

	sign_head = image + 0x200 + data_size;
	sign_size = READ32_LE(sign_head + 0x20);
	sign_offs = READ32_LE(sign_head + 0x28);

	printf("signature data: offs = 0x%x, size = 0x%x\n", sign_offs, sign_size);
	if (sign_offs != 0x200 + data_size + 0x60)
		printf("!!! unexpected signature data offset\n");
	if (file_size < sign_offs)
		ERR_EXIT("signature out of file\n");
	extra_size = file_size - sign_offs;
	if (extra_size < 0x234)
		ERR_EXIT("not enough signature data\n");
	sign_data = image + sign_offs;
	sign_type = READ32_LE(sign_data);
	printf("sign_type = %u\n", sign_type);
	if (sign_type >= 2)
		ERR_EXIT("unknown signature type\n");
	tmp = sign_type * 0x20 + 0x234;
	if (sign_size != tmp)
		printf("!!! wrong signature data size (must be 0x%x)\n", tmp);
	if (extra_size < tmp)
		ERR_EXIT("not enough signature data\n");

	sha256(image + 0x200, data_size, data_hash);
	if (memcmp(sign_data + 0x10c, data_hash, sizeof(data_hash)))
		printf("!!! data hash not match\n");
	key_bits = READ32_LE(sign_data + 4);
	key_size = (key_bits >> 3) + 8;
	printf("public key: bits = %u, size = 0x%x\n", key_bits, key_size);
	if (key_bits > 2048 || (key_bits & 7))
		ERR_EXIT("invalid key size\n");

	{
		uint8_t key_hash[SHA256_DIGEST_LENGTH];
		unsigned i;
		sha256(sign_data + 4, key_size, key_hash);
		printf("key hash:\n");
		for (i = 0; i < 32; i += 16)
			printf("  %08x %08x %08x %08x\n",
					READ32_LE(key_hash + i), READ32_LE(key_hash + i + 4),
					READ32_LE(key_hash + i + 8), READ32_LE(key_hash + i + 12));

		if (sign_type == 1)
			if (memcmp(sign_data + 0x12c, key_hash, sizeof(key_hash)))
				printf("!!! key hash not match\n");
	}


	{
		uint8_t *p = sign_data + sign_type * 0x20 + 0x12c;
		int extra_type, version;
		extra_type = READ32_LE(p);
		version = READ32_LE(p + 4);
		printf("extra_type = %d, version = %d\n", extra_type, version);
		if (extra_type != 1) ERR_EXIT("extra_type must be 1\n");
	}

	{
		BIGNUM *rsa_n, *rsa_e; RSA *rsa;
		uint8_t dec_buf[0x100];
		// RSA_PKCS1_PADDING, RSA_PKCS1_OAEP_PADDING, RSA_NO_PADDING
		int padding = RSA_PKCS1_PADDING;
		int dec_len;

		rsa_n = BN_bin2bn(sign_data + 12, key_bits >> 3, NULL);
		rsa_e = BN_bin2bn(sign_data + 8, 4, NULL);
		if (!rsa_n || !rsa_e) ERR_EXIT("BN_bin2bn failed\n");
		rsa = RSA_new();
		if (!rsa) ERR_EXIT("RSA_new failed\n");
		RSA_set0_key(rsa, rsa_n, rsa_e, NULL);

		memset(dec_buf, 0, sizeof(dec_buf));
		dec_len = RSA_public_decrypt(key_bits >> 3, sign_data + sign_type * 0x20 + 0x134, dec_buf, rsa, padding);
		if (dec_len < 0)
			ERR_EXIT("RSA_public_decrypt failed\n");
		printf("dec_len = %d\n", dec_len);
		tmp = sign_type * 0x20 + 0x28;
		if (dec_len != (int)tmp)
			ERR_EXIT("wrong dec_len (must be %d)\n", tmp);
		if (memcmp(sign_data + 0x10c, dec_buf, tmp))
			printf("!!! decrypted data not match\n");
		RSA_free(rsa);
	}
	return 0;
}

static int sign_image(const char *bin_fn, unsigned sign_type,
		const char *priv_fn, const char *out_fn) {
	uint8_t *image, sign_head[0x60], sign_data[0x254];
	size_t file_size, data_size, extra_size;
	uint8_t data_hash[SHA256_DIGEST_LENGTH];
	uint32_t sign_size, tmp;
	uint32_t key_bits, key_size;
	RSA *rsa;

	if (sign_type >= 2)
		ERR_EXIT("unknown signature type\n");

	image = loadfile(bin_fn, &file_size);
	if (!image)
		ERR_EXIT("loadfile(\"%s\") failed\n", bin_fn);
	if (file_size < 0x200 || memcmp(image, "DHTB", 4))
		ERR_EXIT("bad DHTB header\n");
	tmp = READ32_LE(image + 4);
	if (tmp != 1)
		ERR_EXIT("unknown DHTB version (%u)\n", tmp);
	extra_size = file_size - 0x200;
	data_size = READ32_LE(image + 0x30);
	if (extra_size < data_size)
		ERR_EXIT("bad data size\n");
	if (data_size & 3)
		ERR_EXIT("unaligned data size\n");
	if (extra_size) 
		ERR_EXIT("extra data at the end (maybe already signed)\n");

	memset(sign_head, 0, sizeof(sign_head));
	WRITE32_LE(sign_head + 0x10, data_size);
	WRITE32_LE(sign_head + 0x18, 0x200); // data_offs
	sign_size = sign_type * 0x20 + 0x234;
	WRITE32_LE(sign_head + 0x20, sign_size);
	WRITE32_LE(sign_head + 0x28, file_size + 0x60); // sign_offs

	memset(sign_data, 0, sizeof(sign_data));
	WRITE32_LE(sign_data, sign_type);
	sha256(image + 0x200, data_size, data_hash);
	memcpy(sign_data + 0x10c, data_hash, sizeof(data_hash));

	rsa = rsa_from_file(priv_fn, 1);
	if (!rsa) exit(1);

	{
		const BIGNUM *rsa_n = NULL, *rsa_e = NULL;
		RSA_get0_key(rsa, &rsa_n, &rsa_e, NULL);
		if (!rsa_n || !rsa_e) ERR_EXIT("RSA_get0_key failed\n");

		key_bits = BN_num_bytes(rsa_n) * 8;
		key_size = (key_bits >> 3) + 8;
		printf("public key: bits = %u, size = 0x%x\n", key_bits, key_size);
		if (key_bits > 2048)
			ERR_EXIT("invalid key size\n");

		WRITE32_LE(sign_data + 4, key_bits);
		if (BN_bn2binpad(rsa_e, sign_data + 8, 4) < 0)
			ERR_EXIT("BN_bn2binpad failed\n");
		BN_bn2bin(rsa_n, sign_data + 12);
	}

	if (sign_type == 1) {
		uint8_t key_hash[SHA256_DIGEST_LENGTH];
		sha256(sign_data + 4, key_size, key_hash);
		memcpy(sign_data + 0x12c, key_hash, sizeof(key_hash));
	}

	{
		int padding = RSA_PKCS1_PADDING;
		uint8_t *p = sign_data + sign_type * 0x20 + 0x12c;
		int extra_type = 1, version = ~0;
		WRITE32_LE(p, extra_type);
		WRITE32_LE(p + 4, version);

		tmp = sign_type * 0x20 + 0x28;
		tmp = RSA_private_encrypt(tmp, sign_data + 0x10c, p + 8, rsa, padding);
		printf("enc_len = %d\n", tmp);
		if (tmp != key_bits >> 3)
			ERR_EXIT("RSA_private_encrypt failed\n");
	}

	RSA_free(rsa);

	{
		FILE *fo = fopen(out_fn, "wb");
		if (fo) {
			fwrite(image, 1, file_size, fo);
			fwrite(sign_head, 1, sizeof(sign_head), fo);
			fwrite(sign_data, 1, sign_size, fo);
			fclose(fo);
		}
	}
	return 0;
}

int main(int argc, char **argv) {
	while (argc > 1) {
		if (!strcmp(argv[1], "verify_image")) {
			if (argc <= 2) ERR_EXIT("bad command\n");
			verify_image(argv[2]);
			argv += 2; argc -= 2;
		} else if (!strcmp(argv[1], "sign_image")) {
			if (argc <= 5) ERR_EXIT("bad command\n");
			// $ openssl genrsa -out key.pem 2048
			// $ ./fwcrypto sign_image src.bin {0|1} key.pem signed.bin
			sign_image(argv[2], atoi(argv[3]), argv[4], argv[5]);
			argv += 5; argc -= 5;
		} else {
			ERR_EXIT("unknown command\n");
		}
	}
}

