#include "osapi.h"
#include "eagle_soc.h"
#include "c_types.h"
#include "driver/uart.h"
#include "ets_sys.h"
//#include "missing_declarations.h"

#include "comm.h"
#include "crc16.h"

#define UART0   0
#define UART1   1

// Shift beginnig of buffer so payload is aligned
#define BUF_ALIGN_OFFSET (__BIGGEST_ALIGNMENT__ - 1)

struct decoder {
	uint8_t buf[BUF_ALIGN_OFFSET + MAX_MESSAGE_SIZE];
	uint32_t pos;
	uint8_t in_escape;
	uint8_t err;

	uint16_t crc;
	uint32_t crc_errors;
	uint32_t total_errors;

	comm_callback_t cb;
};

struct encoder {
	uint16_t crc;
};


struct decoder dec_uart0 = {
	.pos = BUF_ALIGN_OFFSET,
	.in_escape = 0,
	.err = 0,
	.crc_errors = 0,
	.total_errors = 0,
	.cb = NULL
};

struct encoder enc_uart0 = {
	.crc = CRC16_CCITT_INIT_VALUE,
};


static inline uint32_t irq_save()
{
	uint32_t level;
	asm volatile ("RSIL %0, 15" : "=r"(level));
	return level;
}

static inline void irq_restore(uint32_t level)
{
	uint32_t tmp, ps;
	// IRQs should be disabled, since we need to do following operations
	// atomically
	level = level & 0xf;
	asm volatile ("RSIL %0, 15\n" : "=r"(tmp));
	asm volatile ("RSR.PS %0" : "=r"(ps));
	ps = (ps & 0xfffffff0) | level;
	asm volatile ("WSR.PS %0" : : "r"(ps));
}

/* ------------------------------------------------------------------ send */

static uint32_t irq_level;

/* FIXME! */
void comm_send_begin(uint8_t c) {
	//ETS_INTR_LOCK();
	/* os_intr_lock(); */
	irq_level = irq_save();
	/* wdt_feed(); */
	comm_send_u8(c);
}

void comm_send_end()
{
	uint16_t crc = enc_uart0.crc;
	comm_send_data((void *)&crc, sizeof(crc));
	uart_tx_one_char(UART0, FRAME_END);
	enc_uart0.crc = CRC16_CCITT_INIT_VALUE;

	irq_restore(irq_level);
	/* ets_intr_unlock(); */
}

void comm_send_u8(uint8_t c) {
	if ((c == FRAME_END) || (c == FRAME_ESC)) {
		uart_tx_one_char(UART0, FRAME_ESC);
		uart_tx_one_char(UART0, c ^ FRAME_XOR);
	} else {
		uart_tx_one_char(UART0, c);
	}
	crc16_ccitt_update(&enc_uart0.crc, c);
}

void comm_send_data(uint8_t *data, int n)
{
	for (; n > 0; n--)
		comm_send_u8(*(data++));
}

void comm_send_status(uint8_t s)
{
	comm_send_begin(MSG_STATUS);
	comm_send_u8(s);
	comm_send_end();
}
/* ------------------------------------------------------------------ receive */

static inline void check_and_dispatch(struct decoder *d)
{
	uint16_t crc_msg;
	uint16_t crc_calc;

	if (d->err) {
		d->total_errors++;
		return;
	}

	if (d->pos == 0)
		return;

	if (d->pos < 1 + 2) {
		d->total_errors++;
		return;
	}

	crc_calc = crc16_ccitt_block(d->buf, d->pos - 2);
	memcpy(&crc_msg, d->buf + d->pos - 2, 2);
	if (crc_calc != crc_msg) {
		d->crc_errors++;
		d->total_errors++;
		return;
	}

	if (d->cb)
		d->cb(d->buf[BUF_ALIGN_OFFSET],
		      &d->buf[BUF_ALIGN_OFFSET + 1],
		      d->pos - BUF_ALIGN_OFFSET - 3);
}

static inline void add_char(struct decoder *d, uint8_t c) {
	if (d->pos >= sizeof(d->buf))
		d->err = 1;
	else
		d->buf[d->pos++] = c;
}

static inline void comm_rx_char(struct decoder *d, uint8_t c)
{
	if (d->in_escape) {
		add_char(d, c ^= FRAME_XOR);
		d->in_escape = 0;
	} else if (c == FRAME_END) {
		check_and_dispatch(d);
		d->pos = BUF_ALIGN_OFFSET;
		d->err = 0;
		d->in_escape = 0;
	} else if (c == FRAME_ESC) {
		d->in_escape = 1;
	} else {
		add_char(d, c);
	}	
}

void uart0_rx_intr_handler(void *para)
{
	uint32_t n;
	uint8 c;

	if (UART_RXFIFO_FULL_INT_ST !=
		(READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
		return;
	}
	WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);

	while (1) {
		n = READ_PERI_REG(UART_STATUS(UART0)) &
			(UART_RXFIFO_CNT << UART_RXFIFO_CNT_S);
		if (!n)
			break;

		c = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
		comm_rx_char(&dec_uart0, c);
	}
}

/* ------------------------------------------------------------------ misc */

uint8_t comm_loglevel = 0;
void comm_set_loglevel(uint8_t level)
{
	comm_loglevel = level;
}

void comm_get_stats(uint32_t *rx_errors, uint32_t *rx_crc_errors) {
	*rx_errors = dec_uart0.total_errors;
	*rx_crc_errors = dec_uart0.crc_errors;
}

void comm_init(comm_callback_t cb) {
	dec_uart0.cb = cb;
	uart_tx_one_char(UART0, FRAME_END);
}
