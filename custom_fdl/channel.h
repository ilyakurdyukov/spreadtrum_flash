#ifndef CHANNEL_H
#define CHANNEL_H

#include "common.h"

typedef struct dl_channel {
	int (*open)(struct dl_channel *channel, int baudrate);
	int (*getchar)(struct dl_channel *channel, int wait);
	int (*write)(struct dl_channel *channel, const void *src, unsigned len);
	int (*close)(struct dl_channel *channel);
	void *priv;
} dl_channel_t;

int dl_getbootmode(void);
dl_channel_t *dl_getchannel(void);

extern dl_channel_t *dl_channel;

#endif  // CHANNEL_H
