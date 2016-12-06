#include "utils/cobs.h"

#define BYTE_EOF               0

enum state
{
  DEC_IDLE = 0,
  DEC_BLOCK,
};

//***********************************************
void cobs_init(struct cobs *cobs)
{
  cobs->state = DEC_IDLE;
}

//***********************************************
uint16_t cobs_encode_size(uint16_t size)
{
  return 1 + size + 1 + ((size + 1) >> 8);
}

//***********************************************
uint16_t cobs_encode(uint16_t const *src, uint16_t src_len, uint16_t *dst, uint16_t dst_len)
{
  uint16_t *p, *code_ptr, code_len;

  if ((src_len + 1 + (src_len >> 8) + 1) > dst_len) // 1 - BYTE_EOF, 1 - overhead
    return 0;
  p = dst;

  code_ptr = p++;
  code_len = 0x01;
  while (src_len--)
  {
    uint16_t ch;

    ch = *src++;
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
  }
  *code_ptr = code_len;
  *p++ = BYTE_EOF;
  return (uint16_t)(p - dst);
}

//***********************************************
uint16_t cobs_encode2(uint16_t *data, uint16_t src_len, uint16_t dst_len)
{
  uint16_t *src, *p, *code_ptr, code_len;
  uint16_t ch, ch_next;

  if ((src_len + 1 + (src_len >> 8) + 1) > dst_len) // 1 - BYTE_EOF, 1 - overhead
    return 0;

  src = p = data;

  code_ptr = p++;
  code_len = 0x01;

  ch = *src++;

  while (src_len--)
  {
    ch_next = *src++;
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

//***********************************************
void cobs_decode(struct cobs *cobs, uint16_t const *src, uint16_t len)
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
          cobs->dec_buf_ind = 0;
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
              if (cobs->dec_buf_ind < cobs->dec_buf_size)
                cobs->dec_buf[cobs->dec_buf_ind++] = BYTE_EOF;
            cobs->block_cnt = cobs->block_len = ch;
          }
          else
          {
            cobs->block_cnt--;
            if (cobs->dec_buf_ind < cobs->dec_buf_size)
              cobs->dec_buf[cobs->dec_buf_ind++] = ch;
          }
        }
        else
        {
          if (cobs->block_cnt == 1)
            cobs->cb(cobs->dec_buf, cobs->dec_buf_ind);
          cobs->dec_buf_ind = 0;
          cobs->state = DEC_IDLE;
        }
      break;
    }
  }
}

//***********************************************
void cobs_decode2(struct cobs *cobs, struct cbuf *cbuf)
{
  uintptr_t i, len;

  i = 0;
  len = cbuf->len;
  while (len--)
  {
    uint16_t ch = cbuf->data[i++];

    switch (cobs->state)
    {
      case DEC_IDLE:
        if (ch != BYTE_EOF)
        {
          cobs->dec_buf_ind = 0;
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
              if (cobs->dec_buf_ind < cobs->dec_buf_size)
                cobs->dec_buf[cobs->dec_buf_ind++] = BYTE_EOF;
            cobs->block_cnt = cobs->block_len = ch;
          }
          else
          {
            cobs->block_cnt--;
            if (cobs->dec_buf_ind < cobs->dec_buf_size)
              cobs->dec_buf[cobs->dec_buf_ind++] = ch;
          }
        }
        else
        {
          if (cobs->block_cnt == 1)
            cobs->cb(cobs->dec_buf, cobs->dec_buf_ind);
          cobs->dec_buf_ind = 0;
          cobs->state = DEC_IDLE;
        }
      break;
    }
  }
}
