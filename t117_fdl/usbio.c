#include "common.h"
#include "channel.h"

void clean_dcache_range(void *start, void *end);
void invalidate_dcache_range(void *start, void *end);

#define USB_BUFSIZE 12800

static char usb_size64; // max bulk size
unsigned usb_recv_idx, usb_recv_len;
static uint8_t usb_recv_buf[USB_BUFSIZE] ALIGN(64);

typedef struct {
	void *ptr;
	uint16_t len1, len2;
	uint32_t flags;
} usb_trans_t;

static usb_trans_t usb_send_trans ALIGN(16);
static usb_trans_t usb_recv_trans ALIGN(16);

static void usr_start_recv(void) {
	uint32_t b = 0x20201be0 + (15 + 6) * 32;
	MEM4(b + 8) |= 0x14;
	MEM4(b + 0x14) = (uint32_t)&usb_recv_trans;
	MEM4(b + 4) |= 1;
}

static const uint8_t dev_desc[] ALIGN(4) = {
	0x12, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x40,
	0x82, 0x17, 0x00, 0x4d, 0xff, 0xff, 0x00, 0x00,
	0x00, 0x01
};

static uint8_t config_desc[] ALIGN(4) = {
	0x09, 0x02, 0x20, 0x00, 0x01, 0x01, 0x00, 0xc0, 0x32,
	0x09, 0x04, 0x00, 0x00, 0x02, 0xff, 0x00, 0x00, 0x00,
	0x07, 0x05, 0x85, 0x02, 0x00, 0x02, 0x00,
	0x07, 0x05, 0x06, 0x02, 0x00, 0x02, 0x00
};

static void usb_ep_config(void) {
	uint32_t b = 0x20200000;
	uint32_t n = usb_size64 ? 64 : 0x200;

	MEM1(b + 0xe) = 5;
	MEM2(b + 0x6) = 0x21;
	MEM2(b + 0x8) = 0x40;
	MEM1(b + 0x62) = 0x17;
	MEM2(b + 0x64) = 0x108;
	MEM2(b + 0x150) = n;
	MEM1(b + 0x152) = 0x48;
	MEM1(b + 0x153) = 0x20;
	MEM1(b + 0x153) |= 0x94;

	MEM1(b + 0xe) = 6;
	MEM1(b + 0x63) = 0x17;
	MEM2(b + 0x66) = 8;
	MEM2(b + 0x164) = n;
	MEM1(b + 0x166) = 0x90;
	MEM1(b + 0x167) |= 0xa8;
}

static void usb_send_desc(uint8_t *buf) {
	unsigned i, n, type;

	MEM1(0x20200012) |= 0x40;

	type = buf[3];
	n = buf[6];

	if (type == 1) {
		if (n > sizeof(dev_desc)) n = sizeof(dev_desc);
		for (i = 0; i < n; i++)
			MEM1(0x20200020) = dev_desc[i];
	} else if (type == 2) {
		if (usb_size64) {
			config_desc[9 + 9 + 4] = 64;
			config_desc[9 + 9 + 5] = 0;
			config_desc[9 + 9 + 7 + 4] = 64;
			config_desc[9 + 9 + 7 + 5] = 0;
		}
		if (n > sizeof(config_desc)) n = sizeof(config_desc);
		for (i = 0; i < n; i++)
			MEM1(0x20200020) = config_desc[i];
	} else return;
	MEM1(0x20200012) |= 0x0a;
}

static void usb_sys_events(void) {
	unsigned i, n;
	uint8_t buf[8];

	if (!(MEM1(0x20200012) & 1)) return;

	for (i = 0; i < 8; i++)
		buf[i++] = MEM1(0x20200020);

	if (buf[0] & 0x60) {
		MEM1(0x20200012) |= 0x48;
		while (!(MEM2(0x20200002) & 1));
		if ((buf[0] & 0x7f) == 0x21 && buf[2]) {
			usb_ep_config();
			usr_start_recv();
		}
	} else if (buf[1]) {
		switch (buf[1]) {
		case 5:
			MEM1(0x20200012) |= 0x48;
			while (!(MEM2(0x20200002) & 1));
			MEM1(0x20200000) = buf[2];
			break;
		case 6:
			usb_send_desc(buf);
			break;
		case 9:
			MEM1(0x20200012) |= 0x48;
			while (!(MEM2(0x20200002) & 1));
			break;
		}
	} else {
		MEM1(0x20200012) |= 0x40;
		n = buf[6] | buf[7] << 8;
		for (i = 0; i < n; i++)
			MEM1(0x20200020) = 0;
		MEM1(0x20200012) |= 0x0a;
	}
}

static unsigned usb_handler(void) {
	uint32_t tmp;

	if (MEM1(0x2020000a) & 4)
		usb_size64 = (MEM1(0x20200001) & 0x10) == 0;

	tmp = MEM4(0x20201e88);
	if (MEM2(0x20200002) & 1) {
		MEM1(0x2020000e) = 0;
		usb_sys_events();
	}
	if (!(tmp & 0x40000)) return 0;
	{
		uint32_t b = 0x20201be0 + (15 + 6) * 32;
		MEM4(b + 8) |= 0x14000000;
		tmp = USB_BUFSIZE - (MEM4(0x20201e90) >> 16);
		usb_recv_len = tmp;
		usb_recv_idx = 0;
		invalidate_dcache_range(usb_recv_buf, usb_recv_buf + tmp);
		MEM4(b + 8) |= 0x14;
		MEM4(b + 0x14) = (uint32_t)&usb_recv_trans;
		MEM4(b + 4) |= 1;
	}
	return 1;
}

static int usb_read1(void) {
	if (usb_recv_idx == usb_recv_len)
		while (!usb_handler());
	return usb_recv_buf[usb_recv_idx++];
}

static void usb_send(const void *src, unsigned len) {
	int ep = 5;
	uint32_t a, b = 0x20201be0 + ep * 32;
	usb_trans_t *tr = &usb_send_trans;

	tr->ptr = (void*)src;
	tr->len1 = 0x20;
	tr->len2 = len;
	tr->flags = 5;

	clean_dcache_range((char*)src, (char*)src + len);
	clean_dcache_range(tr, (char*)tr + 12);

	MEM4(b + 8) |= 4;
	MEM4(b + 0x14) = (uint32_t)tr;
	MEM4(b + 4) |= 1;
#if 1
	while (!((a = MEM4(b + 8)) & 1 << 18));
#else // orig code, that's why FDL1 is so slow!
	a = MEM4(b + 8);
	while (!(a & 1 << 18)) {
		a = MEM4(b + 8);
		DELAY(1000 << 10)
	}
#endif
	MEM4(b + 8) = a | 1 << 26;
}

static void usb_init(void) {
	{
		usb_trans_t *tr = &usb_recv_trans;

		tr->ptr = usb_recv_buf;
		tr->len1 = 0x20;
		tr->len2 = USB_BUFSIZE;
		tr->flags = 5;

		// orig code don't do this
		clean_dcache_range(tr, (char*)tr + 12);
	}
	usb_size64 = 0;
	usb_recv_idx = usb_recv_len = 0;
#if !FDL2
	// clear interrupts from BROM
	{
		uint32_t b = 0x20201be0 + (15 + 6) * 32;
		MEM4(b + 8) |= 0x14000000;
	}
#endif
	usr_start_recv();
}

static int usb_channel_open(dl_channel_t *channel,
		int baudrate) {
	if (!channel->priv) {
		usb_init();
		channel->priv = (void*)1;
	} else {
		// set baudrate
	}
	return 0;
}

static int usb_channel_getchar(dl_channel_t *channel, int wait) {
	return usb_read1();
}

static int usb_channel_write(dl_channel_t *channel,
		const void *src, unsigned len) {
	usb_send(src, len);
	return len;
}

static int usb_channel_close(dl_channel_t *channel) {
	return 0;
}

dl_channel_t dl_usb_channel = {
	usb_channel_open,
	usb_channel_getchar,
	usb_channel_write,
	usb_channel_close,
	NULL
};

