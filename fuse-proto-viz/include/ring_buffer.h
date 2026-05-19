#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdatomic.h>

#define RING_BUFFER_SIZE 1024

struct fuse_event {
    uint64_t timestamp;
    uint32_t opcode;
    uint64_t nodeid;
    char description[128];
};

struct ring_buffer {
    struct fuse_event events[RING_BUFFER_SIZE];
    _Atomic uint32_t write_index;
    _Atomic uint32_t read_index;
};

void ring_buffer_init(struct ring_buffer *rb);
void ring_buffer_write(struct ring_buffer *rb, uint32_t opcode, uint64_t nodeid, const char *desc);
int ring_buffer_read(struct ring_buffer *rb, struct fuse_event *out_event);

#endif /* RING_BUFFER_H */
