#include "common.h"
#include "channel.h"

#define BOOT_FLAG_USB    0x5A
#define BOOT_FLAG_UART1  0x6A
#define BOOT_FLAG_UART0  0x7A

extern dl_channel_t dl_usb_channel;

#define BOOT_FLAG_ADDRESS 0x8b000228

int dl_getbootmode(void) {
	return MEM4(BOOT_FLAG_ADDRESS) >> 8 & 0xff;
}

dl_channel_t *dl_getchannel(void) {
	int mode = dl_getbootmode();
	dl_channel_t *channel;
	switch (mode) {
#if 0
		case BOOT_FLAG_UART0:
			channel = &dl_uart0_channel;
			break;
		case BOOT_FLAG_UART1:
			channel = &dl_uart1_channel;
			break;
#endif
		// case BOOT_FLAG_USB:
		default:
			channel = &dl_usb_channel;
			break;
	}
	return channel;
}

