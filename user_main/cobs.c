#include <inttypes.h>
#include <string.h>
#include "cobs.h"


size_t cobs_encode(uint8_t *dst_orig, uint8_t *src_orig, size_t src_len)
{
	uint8_t *dst, *code_ptr, *code_end, code_len;
	uint8_t const *src;
	size_t dest_sz = COBS_ENCODED_MAX_SIZE(src_len);

	dst = dst_orig;
	src = src_orig;

	code_end = dst + dest_sz - 1; // EOF reservation
	code_ptr = dst++;
	code_len = 0x01;

	while (src_len--)
	{
		uint8_t ch = *src++;

		if (dst >= code_end)
			return -1;

		if (ch == COBS_BYTE_EOF)
		{
			*code_ptr = code_len;
			code_ptr = dst++;
			code_len = 0x01;
		}
		else
		{
			*dst++ = ch;
			code_len++;
			if (code_len == 0xFF)
			{
				if (src_len == 0)
					break;

				*code_ptr = code_len;
				code_ptr = dst++;
				code_len = 0x01;
			}
		}
	}

	*code_ptr = code_len;
	*dst++ = COBS_BYTE_EOF;

	return dst - dst_orig;
}


void cobs_decoder_init(struct cobs_decoder *cobs, uint8_t *buf, size_t buf_size,
		       cobs_callback_t cb, void *cb_data)
{
  cobs->state = DEC_IDLE;
  cobs->buf = buf;
  cobs->buf_size = buf_size;
  cobs->cb = cb;
  cobs->cb_data = cb_data;
}

/* FIXME: check logic if cnt = 0xff */
void cobs_decoder_put(struct cobs_decoder *cobs, uint8_t const *src, size_t len)
{
	while (len--) {
		uint16_t ch;

		ch = *src++;

		switch (cobs->state) {
		case DEC_IDLE:
			if (ch != COBS_BYTE_EOF) {
				cobs->buf_ind = 0;
				cobs->overflow = 0;
				cobs->block_cnt = cobs->block_len = ch;
				cobs->state = DEC_BLOCK;
			}
			break;

		case DEC_BLOCK:
			if (ch != COBS_BYTE_EOF) {
				if ((cobs->block_cnt == 1) && (cobs->block_len < 0xFF)) {
					cobs->buf[cobs->buf_ind++] = COBS_BYTE_EOF;
					cobs->block_cnt = cobs->block_len = ch;
				} else if ((cobs->block_cnt == 1) && (cobs->block_len == 0xff)) {
					cobs->block_cnt = cobs->block_len = ch;
				} else {
					cobs->block_cnt--;
					if (cobs->buf_ind < cobs->buf_size)
						cobs->buf[cobs->buf_ind++] = ch;
					else
						cobs->overflow = 1; // TODO: report error?
				}
			} else {
				if ((cobs->block_cnt == 1) && (cobs->overflow == 0))
					cobs->cb(cobs->cb_data, cobs->buf, cobs->buf_ind);
				cobs->buf_ind = 0;
				cobs->state = DEC_IDLE;
			}
			break;
		}
	}
}
