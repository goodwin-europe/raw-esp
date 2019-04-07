#ifndef _COBS_H_
#define _COBS_H_

#include <stddef.h>
#include <unistd.h>
// #include <inttypes.h>

#define COBS_MAX_DISTANCE (0xff - 1)
#define COBS_BYTE_EOF               0

#define COBS_ENCODED_MAX_SIZE(size) ((size) + (((size) + COBS_MAX_DISTANCE - 1) / COBS_MAX_DISTANCE) + 1)

size_t cobs_encode(uint8_t *, uint8_t *, size_t);


enum cobs_decoder_state
{
  DEC_IDLE = 0,
  DEC_BLOCK,
};

typedef void (*cobs_callback_t)(void *, uint8_t *, size_t);

struct cobs_decoder
{
  uint8_t *buf;
  uint32_t buf_size;
  uint32_t buf_ind;
  uint8_t overflow;

  uint32_t block_len;
  uint32_t block_cnt;
  enum cobs_decoder_state state;

  cobs_callback_t cb;
  void *cb_data;
};

void cobs_decoder_init(struct cobs_decoder *, uint8_t *, size_t, cobs_callback_t cb, void *cb_data);
void cobs_decoder_put(struct cobs_decoder *, uint8_t const *, size_t len);


#endif
