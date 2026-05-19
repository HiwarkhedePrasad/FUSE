#include "ring_buffer.h"
#include <string.h>
#include <time.h>
#include <stdio.h>

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

void ring_buffer_init(struct ring_buffer *rb) {
    atomic_init(&rb->write_index, 0);
    atomic_init(&rb->read_index, 0);
}

void ring_buffer_write(struct ring_buffer *rb, uint32_t opcode, uint64_t nodeid, const char *desc) {
    uint32_t current_write = atomic_load_explicit(&rb->write_index, memory_order_relaxed);
    uint32_t next_write = (current_write + 1) % RING_BUFFER_SIZE;

    rb->events[current_write].timestamp = get_timestamp_ns();
    rb->events[current_write].opcode    = opcode;
    rb->events[current_write].nodeid    = nodeid;
    strncpy(rb->events[current_write].description, desc,
            sizeof(rb->events[current_write].description) - 1);
    rb->events[current_write].description[sizeof(rb->events[current_write].description) - 1] = '\0';

    atomic_store_explicit(&rb->write_index, next_write, memory_order_release);
}

int ring_buffer_read(struct ring_buffer *rb, struct fuse_event *out_event) {
    uint32_t current_read  = atomic_load_explicit(&rb->read_index,  memory_order_relaxed);
    uint32_t current_write = atomic_load_explicit(&rb->write_index, memory_order_acquire);

    if (current_read == current_write) {
        return 0;
    }

    *out_event = rb->events[current_read];

    uint32_t next_read = (current_read + 1) % RING_BUFFER_SIZE;
    atomic_store_explicit(&rb->read_index, next_read, memory_order_release);

    return 1;
}
