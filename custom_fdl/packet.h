#ifndef DL_PACKET_H
#define DL_PACKET_H

#include "common.h"
#include "cmd_def.h"

#define MAX_PKT_SIZE  0x1000
#define MAX_PKT_PAYLOAD  MAX_PKT_SIZE - 8

extern uint8_t packet_temp[MAX_PKT_SIZE * 2 + 4];
void dl_packet_init(void);

static inline uint8_t *dl_send_buf(void) {
	return packet_temp + MAX_PKT_SIZE + 4;
}

uint8_t* dl_get_packet(void);
void dl_send_packet(uint8_t *src);
void dl_send_ack(int type);

#endif  // DL_PACKET_H

