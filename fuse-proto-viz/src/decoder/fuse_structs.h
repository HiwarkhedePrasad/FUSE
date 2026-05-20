#ifndef FUSE_STRUCTS_H
#define FUSE_STRUCTS_H

#include <stdint.h>

/* ── FUSE request header (kernel → userspace) ── */
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

/* ── FUSE response header (userspace → kernel) ── */
struct fuse_out_header {
	uint32_t len;
	int32_t error;
	uint64_t unique;
};

/* ── Opcodes ── */
enum fuse_opcode {
	FUSE_LOOKUP      = 1,
	FUSE_FORGET      = 2,   /* no response expected */
	FUSE_GETATTR     = 3,
	FUSE_SETATTR     = 4,
	FUSE_READLINK    = 5,
	FUSE_SYMLINK     = 6,
	FUSE_MKNOD       = 8,
	FUSE_MKDIR       = 9,
	FUSE_UNLINK      = 10,
	FUSE_RMDIR       = 11,
	FUSE_RENAME      = 12,
	FUSE_LINK        = 13,
	FUSE_OPEN        = 14,
	FUSE_READ        = 15,
	FUSE_WRITE       = 16,
	FUSE_STATFS      = 17,
	FUSE_RELEASE     = 18,
	FUSE_FSYNC       = 20,
	FUSE_SETXATTR    = 21,
	FUSE_GETXATTR    = 22,
	FUSE_LISTXATTR   = 23,
	FUSE_REMOVEXATTR = 24,
	FUSE_FLUSH       = 25,
	FUSE_INIT        = 26,
	FUSE_OPENDIR     = 27,
	FUSE_READDIR     = 28,
	FUSE_RELEASEDIR  = 29,
	FUSE_FSYNCDIR    = 30,
	FUSE_GETLK       = 31,
	FUSE_SETLK       = 32,
	FUSE_SETLKW      = 33,
	FUSE_ACCESS      = 34,
	FUSE_CREATE      = 35,
	FUSE_INTERRUPT   = 36,
	FUSE_BMAP        = 37,
	FUSE_DESTROY     = 38,
	FUSE_IOCTL       = 39,
	FUSE_POLL        = 40,
	FUSE_NOTIFY_REPLY = 41,
	FUSE_BATCH_FORGET = 42,
	FUSE_READDIRPLUS = 43,
	FUSE_RENAME2     = 44,
	FUSE_LSEEK       = 45,
	FUSE_COPY_FILE_RANGE = 46,
};

/* ── FUSE_INIT ── */
struct fuse_init_in {
	uint32_t major;
	uint32_t minor;
	uint32_t max_readahead;
	uint32_t flags;
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

/* ── FUSE_GETATTR ── */
struct fuse_getattr_in {
	uint32_t getattr_flags;
	uint32_t dummy;
	uint64_t fh;
};

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
	uint32_t mode;
	uint32_t nlink;
	uint32_t uid;
	uint32_t gid;
	uint32_t rdev;
	uint32_t blksize;
	uint32_t padding;
};

struct fuse_attr_out {
	uint64_t attr_valid;
	uint32_t attr_valid_nsec;
	uint32_t dummy;
	struct fuse_attr attr;
};

/* ── FUSE_LOOKUP ── */
struct fuse_entry_out {
	uint64_t nodeid;
	uint64_t generation;
	uint64_t entry_valid;
	uint64_t attr_valid;
	uint32_t entry_valid_nsec;
	uint32_t attr_valid_nsec;
	struct fuse_attr attr;
};

/* ── FUSE_OPEN / FUSE_OPENDIR ── */
struct fuse_open_in {
	uint32_t flags;
	uint32_t open_flags;
};

struct fuse_open_out {
	uint64_t fh;
	uint32_t open_flags;
	uint32_t padding;
};

/* ── FUSE_READ ── */
struct fuse_read_in {
	uint64_t fh;
	uint64_t offset;
	uint32_t size;
	uint32_t read_flags;
	uint64_t lock_owner;
	uint32_t flags;
	uint32_t padding;
};

/* ── FUSE_WRITE ── */
struct fuse_write_in {
	uint64_t fh;
	uint64_t offset;
	uint32_t size;
	uint32_t write_flags;
	uint64_t lock_owner;
	uint32_t flags;
	uint32_t padding;
};

struct fuse_write_out {
	uint32_t size;
	uint32_t padding;
};

/* ── FUSE_RELEASE / FUSE_RELEASEDIR ── */
struct fuse_release_in {
	uint64_t fh;
	uint32_t flags;
	uint32_t release_flags;
	uint64_t lock_owner;
};

/* ── FUSE_READDIR / FUSE_READDIRPLUS ── */
struct fuse_dirent {
	uint64_t ino;
	uint64_t off;
	uint32_t namelen;
	uint32_t type;
	char name[];
};

/* Dirent types (matches Linux DT_* constants) */
#define FUSE_DT_UNKNOWN  0
#define FUSE_DT_FIFO     1
#define FUSE_DT_CHR      2
#define FUSE_DT_DIR      4
#define FUSE_DT_BLK      6
#define FUSE_DT_REG      8
#define FUSE_DT_LNK     10
#define FUSE_DT_SOCK    12
#define FUSE_DT_WHT     14

/* ── FUSE_ACCESS ── */
struct fuse_access_in {
	uint32_t mask;
	uint32_t padding;
};

/* ── FUSE_STATFS ── */
struct fuse_kstatfs {
	uint64_t blocks;
	uint64_t bfree;
	uint64_t bavail;
	uint64_t files;
	uint64_t ffree;
	uint32_t bsize;
	uint32_t namelen;
	uint32_t frsize;
	uint32_t padding;
	uint32_t spare[6];
};

struct fuse_statfs_out {
	struct fuse_kstatfs st;
};

/* ── FUSE_FORGET (no response expected) ── */
struct fuse_forget_in {
	uint64_t nlookup;
};

/* ── FUSE_ROOT_ID ── */
#define FUSE_ROOT_ID 1

/* ── Linux error codes ── */
#ifndef ENOENT
#define ENOENT 2
#endif
#ifndef ENOSYS
#define ENOSYS 38
#endif
#ifndef EINVAL
#define EINVAL 22
#endif
#ifndef EACCES
#define EACCES 13
#endif

/* FUSE_INIT flags we support */
#define FUSE_ASYNC_READ       (1 << 0)
#define FUSE_POSIX_LOCKS      (1 << 1)
#define FUSE_FILE_OPS         (1 << 2)
#define FUSE_ATOMIC_O_TRUNC   (1 << 3)
#define FUSE_EXPORT_SUPPORT   (1 << 4)
#define FUSE_BIG_WRITES       (1 << 5)
#define FUSE_DONT_MASK        (1 << 6)
#define FUSE_SPLICE_WRITE     (1 << 7)
#define FUSE_SPLICE_MOVE      (1 << 8)
#define FUSE_SPLICE_READ      (1 << 9)
#define FUSE_FLOCK_LOCKS      (1 << 10)
#define FUSE_HAS_IOCTL_DIR    (1 << 11)
#define FUSE_AUTO_INVAL_DATA  (1 << 12)
#define FUSE_DO_READDIRPLUS   (1 << 13)
#define FUSE_READDIRPLUS_AUTO (1 << 14)
#define FUSE_ASYNC_DIO        (1 << 15)
#define FUSE_WRITEBACK_CACHE  (1 << 16)
#define FUSE_NO_OPEN_SUPPORT  (1 << 17)

#endif /* FUSE_STRUCTS_H */
