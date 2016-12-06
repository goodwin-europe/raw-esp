#ifndef _COBS_H_
#define _COBS_H_

#include <stdint.h>

struct cobs
{
  void (* cb)(uint8_t const *src, uint32_t len);
  uint8_t *dec_buf;
  uint32_t dec_buf_size;
  uint32_t dec_buf_ind;

  uint32_t block_len;
  uint32_t block_cnt;
  uint8_t state;
};

void cobs_init(struct cobs *cobs);

uint32_t cobs_count_size(uint32_t size);

uint32_t cobs_encode(uint8_t const *src, uint32_t src_len, uint8_t *dst, uint32_t dst_len);
void cobs_decode(struct cobs *cobs, uint8_t const *src, uint32_t len);

#endif
