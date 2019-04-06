/* TODO:
 * - stop dispatching packets directly from interrupt handler context,
 * - stop disabling interrupts during packet transmission.
 *
 * Both features will need either packet queues + dynamic allocation or
 * two big static atomic ringbuffers.
 */
#include "osapi.h"
#include "eagle_soc.h"
#include "c_types.h"
#include "driver/uart.h"
#include "ets_sys.h"
#include "user_interface.h"
#include "mem.h"
//#include "missing_declarations.h"

#include "comm.h"
#include "misc.h"
#include "cobs.h"
#include "crc16.h"

#define UART0   0
#define UART1   1

// Shift beginnig of the buffer so payload is aligned
#define BUF_ALIGN_OFFSET (__BIGGEST_ALIGNMENT__ - 1)

struct decoder {
	struct cobs_decoder cobs;
	uint8_t buf[BUF_ALIGN_OFFSET + COBS_ENCODED_SIZE(MAX_MESSAGE_SIZE)];

	uint32_t proto_errors;
	uint32_t crc_errors;
	comm_callback_t cb;
};

struct encoder {
	uint8_t buf[COBS_ENCODED_SIZE(MAX_MESSAGE_SIZE)];
	size_t idx;
};


struct decoder dec_uart0;
struct encoder enc_uart0;


/* ------------------------------------------------------------------ send */
/* TX implementation here is quite hacky and is inherited from times when
   simple HDLC-like framing was used. It was possible to transfer several
   chunks of data prepared on stack directly to the UART FIFO, and then
   send end of frame.

   Currently used COBS encoder has to see all frame at once, so we copy
   chunks of data into a static buffer. When frame is complete, it's encoded
   and sent to UART. Since I don't use any OS-level locks, interrupt has
   to be disabled since beginning of this process to the end to provide
   exclusive access to encoder. That's not good and code should be
   refactored later (hopefully).
*/

static uint32_t irq_level;

void ICACHE_FLASH_ATTR
encoder_init(struct encoder *e)
{
	e->idx = 0;
}

bool ICACHE_FLASH_ATTR
encoder_put_data(struct encoder *e, void *data, size_t len)
{
	if (e->idx + len > sizeof(e->buf)) {
		return FALSE;
	}

	memcpy(e->buf + e->idx, data, len);
	e->idx += len;

	return TRUE;
}

bool ICACHE_FLASH_ATTR
encoder_finalize(struct encoder *e)
{
	if (COBS_ENCODED_SIZE(e->idx + 2) > sizeof(e->buf)) {
		return FALSE;
	}

	uint16_t crc = crc16_block(e->buf, e->idx);
	encoder_put_data(e, &crc, sizeof(crc)); /* Caution: assule LE here */

	ssize_t final_size = cobs_encode(e->buf, e->idx, sizeof(e->buf));
	if (final_size < 0) {
		e->idx = 0;
		return FALSE;
	} else {
		e->idx = final_size;
		return TRUE;
	}
}

void ICACHE_FLASH_ATTR
comm_send_begin(uint8_t c) {
	irq_level = irq_save();
	encoder_put_data(&enc_uart0, &c, 1);
}

void ICACHE_FLASH_ATTR
comm_send_u8(uint8_t c) {
	encoder_put_data(&enc_uart0, &c, 1);
}

void ICACHE_FLASH_ATTR
comm_send_data(uint8_t *data, size_t n)
{
	encoder_put_data(&enc_uart0, data, n);
}

void ICACHE_FLASH_ATTR
comm_send_status(uint8_t s)
{
	comm_send_begin(MSG_STATUS);
	comm_send_u8(s);
	comm_send_end();
}

void ICACHE_FLASH_ATTR
comm_send_end()
{
	size_t i;

	encoder_finalize(&enc_uart0);
	for (i = 0; i < enc_uart0.idx; i++)
		uart_tx_one_char(UART0, enc_uart0.buf[i]);
	enc_uart0.idx = 0;

	irq_restore(irq_level);
	/* ets_intr_unlock(); */
}


/* ------------------------------------------------------------------ receive */

static inline void ICACHE_FLASH_ATTR
decoder_check_and_dispatch_cb(void *decoder, uint8_t *data, size_t len)
{
	struct decoder *dec = decoder;
	uint16_t crc_msg;
	uint16_t crc_calc;

	if (len < 3) {
		dec->proto_errors ++;
		return;
	}

	crc_calc = crc16_block(data, len - 2);
	memcpy(&crc_msg, data + len - 2, 2);
	if (crc_calc != crc_msg) {
		dec->crc_errors++;
		return;
	}

	if (dec->cb)
		dec->cb(data[0], data + 1, len - 3);
}

static inline void ICACHE_FLASH_ATTR
decoder_init(struct decoder *dec, comm_callback_t cb)
{
	cobs_decoder_init(
		&dec->cobs,
		dec->buf + BUF_ALIGN_OFFSET, sizeof(dec->buf) - BUF_ALIGN_OFFSET,
		decoder_check_and_dispatch_cb, dec);
	dec->proto_errors = 0;
	dec->crc_errors = 0;
	dec->cb = cb;
}

static inline void ICACHE_FLASH_ATTR
decoder_put_data(struct decoder *dec, void *data, size_t len)
{
	cobs_decoder_put(&dec->cobs, data, len);
}


static inline void ICACHE_FLASH_ATTR
do_rx()
{
	uint8_t buf[64];
	uint32_t i, n;
	uint8 c;

	while (1) {
		n = READ_PERI_REG(UART_STATUS(UART0)) &
			(UART_RXFIFO_CNT << UART_RXFIFO_CNT_S);
		if (!n)
			break;

		n = MIN(n, sizeof(buf));
		for (i = 0; i < n; i++) {
		    buf[i] = READ_PERI_REG(UART_FIFO(UART0)) & 0xFF;
		}
		decoder_put_data(&dec_uart0, buf, n);
	}

	uart0_rx_intr_enable();
}

static inline void ICACHE_FLASH_ATTR
do_tx()
{
	// TODO: implement
}

static inline void ICACHE_FLASH_ATTR
comm_task(os_event_t *e)
{
	switch (e->sig) {
	case DO_RX: do_rx(); break;
	case DO_TX: do_tx(); break;
	default: COMM_ERR("unknown task variant");
	}
}

// FIXME name. Actually it handles all uart events, not only rx.
void uart0_rx_intr_handler(void *para)
{
	if (UART_RXFIFO_FULL_INT_ST !=
		(READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
		return;
	}
	uart0_rx_intr_disable();
	WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
	system_os_post(COMM_PRIO, DO_RX, 0);
}

/* ------------------------------------------------------------------ misc */

uint8_t comm_loglevel = 0;
void ICACHE_FLASH_ATTR
comm_set_loglevel(uint8_t level)
{
	comm_loglevel = level;
}

void ICACHE_FLASH_ATTR
comm_get_stats(uint32_t *rx_errors, uint32_t *rx_crc_errors) {
	*rx_errors = dec_uart0.proto_errors + dec_uart0.crc_errors;
	*rx_crc_errors = dec_uart0.crc_errors;
}

/* os_event_t comm_queue[2]; */

void ICACHE_FLASH_ATTR
comm_init(comm_callback_t cb) {
	encoder_init(&enc_uart0);
	decoder_init(&dec_uart0, cb);

	os_event_t *comm_queue = (void *)os_malloc(sizeof(os_event_t) * 2);
	system_os_task(comm_task, COMM_PRIO, comm_queue, 2);//ARRAY_SIZE(comm_queue));
	// init enables rx interrupt
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	uart_tx_one_char(UART0, COBS_BYTE_EOF);
}
