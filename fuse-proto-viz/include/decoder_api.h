#ifndef DECODER_API_H
#define DECODER_API_H

#include <stdint.h>
#include "ring_buffer.h"

void decode_fuse_message(char *raw_buffer, int bytes_read, int fuse_fd, struct ring_buffer *rb);

#endif /* DECODER_API_H */
