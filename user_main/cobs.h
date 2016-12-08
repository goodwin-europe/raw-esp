#ifndef _COBS_H_
#define _COBS_H_

#include <stddef.h>
#include <unistd.h>
#include "c_types.h"

#define COBS_MAX_DISTANCE (0xff - 1)
#define COBS_BYTE_EOF               0
/* Zero marker at the head, actual data, terminating zero, and extra marker
   for every 0xFD bytes */
#define COBS_ENCODED_SIZE(size) (1 + (size) + 1 + ((size) / (COBS_MAX_DISTANCE - 1)))

ssize_t cobs_encode(uint8_t *, size_t, size_t);


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

  uint32_t block_len;
  uint32_t block_cnt;
  enum cobs_decoder_state state;

  cobs_callback_t cb;
  void *cb_data;
};

void cobs_decoder_init(struct cobs_decoder *, uint8_t *, size_t, cobs_callback_t cb, void *cb_data);
void cobs_decoder_put(struct cobs_decoder *, uint8_t const *, size_t len);


#endif
