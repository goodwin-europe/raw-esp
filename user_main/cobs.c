#include "cobs.h"

#define BYTE_EOF               0

/* encodes in place */
ssize_t cobs_encode(uint8_t *data, size_t src_len, size_t dst_len)
{
  uint8_t *src, *p, *code_ptr, code_len;
  uint8_t ch;

  if ((src_len + 1 + (src_len >> 8) + 1) > dst_len) // 1 - BYTE_EOF, 1 - overhead
    return 0;

  src = p = data;

  code_ptr = p++;
  code_len = 0x01;

  ch = *src++;

  while (src_len--)
  {
    uint8_t ch_next = *src++;
    if (ch == BYTE_EOF)
    {
      *code_ptr = code_len;
      code_ptr = p++;
      code_len = 0x01;
    }
    else
    {
      *p++ = ch;
      code_len++;
      if (code_len == 0xFF)
      {
        *code_ptr = code_len;
        code_ptr = p++;
        code_len = 0x01;
      }
    }
    ch = ch_next;
  }

  *code_ptr = code_len;
  *p++ = BYTE_EOF;

  return (uint16_t)(p - data);
}


void cobs_decoder_init(struct cobs_decoder *cobs, uint8_t *buf, size_t buf_size)
{
  cobs->state = DEC_IDLE;
  cobs->buf = buf;
  cobs->buf_size = buf_size;
  cobs->cb = cb;
}

void cobs_decoder_put(struct cobs_decoder *cobs, uint8_t const *src, size_t len)
{
  while (len--)
  {
    uint16_t ch;

    ch = *src++;

    switch (cobs->state)
    {
      case DEC_IDLE:
        if (ch != BYTE_EOF)
        {
          cobs->buf_ind = 0;
          cobs->block_cnt = cobs->block_len = ch;
          cobs->state = DEC_BLOCK;
        }
      break;

      case DEC_BLOCK:
        if (ch != BYTE_EOF)
        {
          if (cobs->block_cnt == 1)
          {
            if (cobs->block_len < 0xFF)
              if (cobs->buf_ind < cobs->buf_size)
                cobs->buf[cobs->buf_ind++] = BYTE_EOF;
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
            cobs->cb(cobs->buf, cobs->buf_ind);
          cobs->buf_ind = 0;
          cobs->state = DEC_IDLE;
        }
      break;
    }
  }
}
