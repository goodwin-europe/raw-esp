#ifndef COMM_H
#define COMM_H
#include "message.h"

/* Async HDLC-like framing */
#define FRAME_END 0x7e
#define FRAME_ESC 0x7d
#define FRAME_XOR 0x20


typedef void (*comm_callback_t)(uint8_t type, uint8_t *data, uint32_t len);

void comm_init(comm_callback_t cb);
void comm_send_begin(uint8_t c);
void comm_send_u8(uint8_t);
void comm_send_data(uint8_t *, size_t);
void comm_send_end();
void comm_send_status(uint8_t);

void comm_get_stats(uint32_t *, uint32_t *);

extern uint8_t comm_loglevel;

void comm_set_loglevel(uint8_t level);

#define PRINT_BUF_SIZE 128
#define COMM_LOG(level, ...) do {		   \
	if (level >= comm_loglevel) { \
		unsigned char __print_buf[PRINT_BUF_SIZE]; \
		os_sprintf(__print_buf, __VA_ARGS__); \
		comm_send_u8(MSG_LOG); \
		comm_send_u8(level); \
		comm_send_data(__print_buf, strlen(__print_buf)); \
		comm_send_end(); \
	} \
} while(0)

#define COMM_DBG(...)  COMM_LOG(10, __VA_ARGS__)
#define COMM_INFO(...) COMM_LOG(20, __VA_ARGS__)
#define COMM_WARN(...) COMM_LOG(30, __VA_ARGS__)
#define COMM_ERR(...)  COMM_LOG(40, __VA_ARGS__)
#define COMM_CRIT(...) COMM_LOG(50, __VA_ARGS__)

#endif
