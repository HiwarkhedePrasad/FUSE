#ifndef FUSE_STRUCTS_H
#define FUSE_STRUCTS_H

#include <stdint.h>

struct fuse_in_header {
	uint32_t len;
	uint32_t opcode;
	uint64_t unique;
	uint64_t nodeid;
	uint32_t uid;
	uint32_t gid;
	uint32_t pid;
	uint32_t padding;
};

enum fuse_opcode {
	FUSE_LOOKUP = 1,
	FUSE_GETATTR = 3,
	FUSE_OPEN = 14,
	FUSE_READ = 15,
	FUSE_WRITE = 16,
	FUSE_RELEASE = 18,
	FUSE_INIT = 26,
	FUSE_READDIR = 28
};

struct fuse_getattr_in {
	uint32_t getattr_flags;
	uint32_t dummy;
	uint64_t fh;
};

struct fuse_open_in {
	uint32_t flags;
	uint32_t open_flags;
};

struct fuse_release_in {
	uint64_t fh;
	uint32_t flags;
	uint32_t release_flags;
	uint64_t lock_owner;
};

struct fuse_read_in {
	uint64_t fh;
	uint64_t offset;
	uint32_t size;
	uint32_t read_flags;
	uint64_t lock_owner;
	uint32_t flags;
	uint32_t padding;
};

struct fuse_write_in {
	uint64_t fh;
	uint64_t offset;
	uint32_t size;
	uint32_t write_flags;
	uint64_t lock_owner;
	uint32_t flags;
	uint32_t padding;
};

struct fuse_init_in {
	uint32_t major;
	uint32_t minor;
	uint32_t max_readahead;
	uint32_t flags;
};

struct fuse_out_header {
	uint32_t len;
	int32_t error;
	uint64_t unique;
};

struct fuse_init_out {
	uint32_t major;
	uint32_t minor;
	uint32_t max_readahead;
	uint32_t flags;
	uint16_t max_background;
	uint16_t congestion_threshold;
	uint32_t max_write;
	uint32_t time_gran;
	uint32_t unused[9];
};

// The file attribute struct (similar to standard Linux 'stat')
struct fuse_attr {
	uint64_t ino;
	uint64_t size;
	uint64_t blocks;
	uint64_t atime;
	uint64_t mtime;
	uint64_t ctime;
	uint32_t atimensec;
	uint32_t mtimensec;
	uint32_t ctimensec;
	uint32_t mode;      // This determines if it's a file or folder!
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t rdev;
	uint32_t blksize;
	uint32_t padding;
};

// The payload for the GETATTR response
struct fuse_attr_out {
	uint64_t attr_valid;       // Cache timeout (seconds)
	uint32_t attr_valid_nsec;  // Cache timeout (nanoseconds)
	uint32_t dummy;
	struct fuse_attr attr;     // The actual attributes
};

#endif /* FUSE_STRUCTS_H */
