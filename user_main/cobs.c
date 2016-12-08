#include <string.h>
#include "cobs.h"

ssize_t cobs_encode(uint8_t *data, size_t len, size_t buffer_size)
{
	size_t max_encoded_len = COBS_ENCODED_SIZE(len);
	if (buffer_size < max_encoded_len)
		return -1;

	size_t max_overhead = max_encoded_len - len;
	uint8_t *src = data + max_overhead;
	uint8_t *marker = data;
	uint8_t *dst = data + 1;

	memmove(src, data, len);

	/* It's possible to keep `distance` directly in `*marker`, but I'm not
           sure that compiler will figure out aliasing */
	size_t distance = 1;
	while (src != data + max_overhead + len) {
		if (distance > COBS_MAX_DISTANCE) {
			*marker = distance;
			marker = dst;
			dst++;
			distance = 1;
		}

		if (*src == COBS_BYTE_EOF) {
			*marker = distance;
			marker = dst;
			distance = 1;
		} else {
			*dst = *src;
			distance++;
		}
		src++;
		dst++;
	}
	*marker = distance;
	*dst++ = COBS_BYTE_EOF;
	return dst - data;
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
  while (len--)
  {
    uint16_t ch;

    ch = *src++;

    switch (cobs->state)
    {
      case DEC_IDLE:
        if (ch != COBS_BYTE_EOF)
        {
          cobs->buf_ind = 0;
          cobs->block_cnt = cobs->block_len = ch;
          cobs->state = DEC_BLOCK;
        }
      break;

      case DEC_BLOCK:
        if (ch != COBS_BYTE_EOF)
        {
          if (cobs->block_cnt == 1)
          {
            if (cobs->block_len < 0xFF)
              if (cobs->buf_ind < cobs->buf_size)
                cobs->buf[cobs->buf_ind++] = COBS_BYTE_EOF;
            cobs->block_cnt = cobs->block_len = ch;
          }
          else
          {
            cobs->block_cnt--;
            if (cobs->buf_ind < cobs->buf_size)
              cobs->buf[cobs->buf_ind++] = ch;
          }
        }
        else
        {
          if (cobs->block_cnt == 1)
            cobs->cb(cobs->cb_data, cobs->buf, cobs->buf_ind);
          cobs->buf_ind = 0;
          cobs->state = DEC_IDLE;
        }
      break;
    }
  }
}
