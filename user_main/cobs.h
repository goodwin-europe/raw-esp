#ifndef _COBS_H_
#define _COBS_H_

#include <stddef.h>
#include <unistd.h>
#include <stdint.h>

#define COBS_ENCODED_SIZE(size) (1 + (size) + 1 + (((size) + 1) >> 8))
ssize_t cobs_encode(uint8_t *, size_t, size_t);


enum cobs_decoder_state
{
  DEC_IDLE = 0,
  DEC_BLOCK,
};

struct cobs_decoder
{
  void (*cb)(uint8_t const *src, size_t len);
  uint8_t *buf;
  uint32_t buf_size;
  uint32_t buf_ind;

  uint32_t block_len;
  uint32_t block_cnt;
  enum cobs_decoder_state state;
};

void cobs_decoder_init(struct cobs_decoder *, uint8_t *, size_t);
void cobs_decoder_put(struct cobs_decoder *, uint8_t const *, size_t len);


#endif
