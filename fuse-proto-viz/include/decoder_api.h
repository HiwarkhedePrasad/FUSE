#ifndef DECODER_API_H
#define DECODER_API_H

#include <stdint.h>

void decode_fuse_message(char *raw_buffer, int bytes_read, int fuse_fd);

#endif /* DECODER_API_H */
