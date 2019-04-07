#ifndef COMM_H
#define COMM_H
#include "message.h"

#define COMM_TX_PRIO_LOW 0
#define COMM_TX_PRIO_MEDIUM 1
#define COMM_TX_PRIO_HIGH 2


typedef void (*comm_callback_t)(uint8_t type, uint8_t *data, uint32_t len);

void comm_init(comm_callback_t cb);
void comm_get_stats(uint32_t *, uint32_t *, uint32_t *);

void comm_send(uint8_t, void *, size_t n, size_t);
void comm_send_ctl(uint8_t, void *, size_t n);
void comm_send_packet(uint8_t, void *, size_t n);
void comm_send_status(uint8_t s);

extern uint8_t comm_loglevel;

void comm_set_loglevel(uint8_t level);

#define PRINT_BUF_SIZE 128
#define COMM_LOG(level, ...) do { \
	if (level >= comm_loglevel) { \
		unsigned char __print_buf[PRINT_BUF_SIZE]; \
		__print_buf[0] = level; \
		os_sprintf(__print_buf + 1, __VA_ARGS__); \
		comm_send_ctl(MSG_LOG, __print_buf, strlen(__print_buf + 1) + 1);  \
	} \
} while(0)

#define COMM_DBG(...)  COMM_LOG(10, __VA_ARGS__)
#define COMM_INFO(...) COMM_LOG(20, __VA_ARGS__)
#define COMM_WARN(...) COMM_LOG(30, __VA_ARGS__)
#define COMM_ERR(...)  COMM_LOG(40, __VA_ARGS__)
#define COMM_CRIT(...) COMM_LOG(50, __VA_ARGS__)

#define RAW_PRINT(...) do { \
	unsigned char __print_buf[PRINT_BUF_SIZE]; \
	os_sprintf(__print_buf + 1, __VA_ARGS__); \
	uart0_sendStrxx(__print_buf); \
} while(0)

#endif
