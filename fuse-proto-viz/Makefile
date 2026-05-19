CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2 -pthread
LDFLAGS  = -lpthread
INCLUDES = -Iinclude -Isrc/decoder

SRCS = src/interceptor/fuse_interceptor.c \
       src/interceptor/fuse_mount.c \
       src/interceptor/ring_buffer.c \
       src/decoder/fuse_decoder.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: fuse_viz

fuse_viz: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f fuse_viz $(OBJS)
