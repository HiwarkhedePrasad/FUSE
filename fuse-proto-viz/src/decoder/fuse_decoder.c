#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>

#include "fuse_structs.h"
#include "decoder_api.h"
#include "ring_buffer.h"

/* ═══════════════════════════════════════════════════════════════
 *  Helper: send an error-only reply to the kernel
 * ═══════════════════════════════════════════════════════════════ */
static void send_error_reply(int fuse_fd, uint64_t unique, int32_t error) {
	struct fuse_out_header out_header;
	memset(&out_header, 0, sizeof(out_header));
	out_header.len    = sizeof(struct fuse_out_header);
	out_header.error  = error;
	out_header.unique = unique;

	struct iovec iov[1];
	iov[0].iov_base = &out_header;
	iov[0].iov_len  = sizeof(out_header);

	if (writev(fuse_fd, iov, 1) < 0) {
		fprintf(stderr, "[FUSE] writev error reply failed: %s\n", strerror(errno));
	}
}

/* ═══════════════════════════════════════════════════════════════
 *  Helper: send a success reply with a payload
 * ═══════════════════════════════════════════════════════════════ */
static void send_reply(int fuse_fd, uint64_t unique,
                       const void *payload, size_t payload_len) {
	struct fuse_out_header out_header;
	memset(&out_header, 0, sizeof(out_header));
	out_header.len    = (uint32_t)(sizeof(struct fuse_out_header) + payload_len);
	out_header.error  = 0;
	out_header.unique = unique;

	struct iovec iov[2];
	iov[0].iov_base = &out_header;
	iov[0].iov_len  = sizeof(out_header);
	iov[1].iov_base = (void *)payload;
	iov[1].iov_len  = payload_len;

	if (writev(fuse_fd, iov, 2) < 0) {
		fprintf(stderr, "[FUSE] writev reply failed: %s\n", strerror(errno));
	}
}

/* ═══════════════════════════════════════════════════════════════
 *  Helper: add one directory entry to a readdir buffer
 *  Returns bytes written, or 0 if not enough room.
 *  Each entry is padded to 8-byte alignment.
 * ═══════════════════════════════════════════════════════════════ */
static int add_dir_entry(char *buf, int bufsize,
                         uint64_t ino, uint64_t off,
                         const char *name, uint32_t type)
{
	size_t namelen  = strlen(name);
	size_t entlen   = sizeof(struct fuse_dirent) + namelen;
	size_t entlen_padded = (entlen + 7) & ~(size_t)7;

	if ((int)entlen_padded > bufsize)
		return 0;

	struct fuse_dirent *de = (struct fuse_dirent *)buf;
	de->ino     = ino;
	de->off     = off;
	de->namelen = (uint32_t)namelen;
	de->type    = type;

	memcpy(de->name, name, namelen);
	memset(buf + sizeof(struct fuse_dirent) + namelen, 0,
	       entlen_padded - sizeof(struct fuse_dirent) - namelen);

	return (int)entlen_padded;
}

/* ═══════════════════════════════════════════════════════════════
 *  Opcode name lookup
 * ═══════════════════════════════════════════════════════════════ */
static const char *opcode_name(uint32_t op) {
	switch (op) {
		case FUSE_LOOKUP:      return "FUSE_LOOKUP";
		case FUSE_FORGET:      return "FUSE_FORGET";
		case FUSE_GETATTR:     return "FUSE_GETATTR";
		case FUSE_SETATTR:     return "FUSE_SETATTR";
		case FUSE_READLINK:    return "FUSE_READLINK";
		case FUSE_SYMLINK:     return "FUSE_SYMLINK";
		case FUSE_MKNOD:       return "FUSE_MKNOD";
		case FUSE_MKDIR:       return "FUSE_MKDIR";
		case FUSE_UNLINK:      return "FUSE_UNLINK";
		case FUSE_RMDIR:       return "FUSE_RMDIR";
		case FUSE_RENAME:      return "FUSE_RENAME";
		case FUSE_LINK:        return "FUSE_LINK";
		case FUSE_OPEN:        return "FUSE_OPEN";
		case FUSE_READ:        return "FUSE_READ";
		case FUSE_WRITE:       return "FUSE_WRITE";
		case FUSE_STATFS:      return "FUSE_STATFS";
		case FUSE_RELEASE:     return "FUSE_RELEASE";
		case FUSE_FSYNC:       return "FUSE_FSYNC";
		case FUSE_SETXATTR:    return "FUSE_SETXATTR";
		case FUSE_GETXATTR:    return "FUSE_GETXATTR";
		case FUSE_LISTXATTR:   return "FUSE_LISTXATTR";
		case FUSE_REMOVEXATTR: return "FUSE_REMOVEXATTR";
		case FUSE_FLUSH:       return "FUSE_FLUSH";
		case FUSE_INIT:        return "FUSE_INIT";
		case FUSE_OPENDIR:     return "FUSE_OPENDIR";
		case FUSE_READDIR:     return "FUSE_READDIR";
		case FUSE_RELEASEDIR:  return "FUSE_RELEASEDIR";
		case FUSE_FSYNCDIR:    return "FUSE_FSYNCDIR";
		case FUSE_ACCESS:      return "FUSE_ACCESS";
		case FUSE_CREATE:      return "FUSE_CREATE";
		case FUSE_INTERRUPT:   return "FUSE_INTERRUPT";
		case FUSE_BMAP:        return "FUSE_BMAP";
		case FUSE_DESTROY:     return "FUSE_DESTROY";
		case FUSE_IOCTL:       return "FUSE_IOCTL";
		case FUSE_POLL:        return "FUSE_POLL";
		case FUSE_BATCH_FORGET:return "FUSE_BATCH_FORGET";
		case FUSE_READDIRPLUS: return "FUSE_READDIRPLUS";
		case FUSE_RENAME2:     return "FUSE_RENAME2";
		case FUSE_LSEEK:       return "FUSE_LSEEK";
		default:               return "UNKNOWN";
	}
}

/* ═══════════════════════════════════════════════════════════════
 *  Main decoder — handles multiple messages in one read()
 *
 *  CRITICAL: Every FUSE request MUST get a response.
 *  If we don't respond, the kernel blocks the calling process
 *  and sends no more requests.
 * ═══════════════════════════════════════════════════════════════ */
void decode_fuse_message(char *raw_buffer, int bytes_read, int fuse_fd, struct ring_buffer *rb) {
	int offset = 0;

	while (offset < bytes_read) {
		int remaining = bytes_read - offset;

		if (remaining < (int)sizeof(struct fuse_in_header)) {
			fprintf(stderr, "[DECODE] Truncated header: %d bytes left\n", remaining);
			return;
		}

		struct fuse_in_header *header = (struct fuse_in_header *)(raw_buffer + offset);

		if (header->len < sizeof(struct fuse_in_header) || header->len > (uint32_t)remaining) {
			fprintf(stderr, "[DECODE] Bad msg len=%u (remaining=%d)\n",
			       header->len, remaining);
			return;
		}

		const char *opname = opcode_name(header->opcode);

		/* Log every incoming request */
		char _top_desc[128];
		snprintf(_top_desc, sizeof(_top_desc),
			"<< %s op=%u node=%lu len=%u uniq=%lu",
			opname, header->opcode, (unsigned long)header->nodeid,
			header->len, (unsigned long)header->unique);
		ring_buffer_write(rb, header->opcode, header->nodeid, _top_desc);

		char *payload = raw_buffer + offset + sizeof(struct fuse_in_header);
		int payload_bytes = (int)header->len - (int)sizeof(struct fuse_in_header);

		switch (header->opcode) {

		/* ─── FUSE_INIT ─── */
		case FUSE_INIT: {
			if (payload_bytes < (int)sizeof(struct fuse_init_in)) {
				fprintf(stderr, "[DECODE] Short FUSE_INIT payload (%d bytes)\n", payload_bytes);
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_init_in *init_data = (struct fuse_init_in *)payload;

			/* Diagnostic: print what the kernel sent */
			fprintf(stderr, "[FUSE_INIT] kernel wants version %u.%u, max_readahead=%u, flags=0x%x\n",
				init_data->major, init_data->minor,
				init_data->max_readahead, init_data->flags);
			fprintf(stderr, "[FUSE_INIT] sizeof(fuse_init_out)=%zu, sizeof(fuse_out_header)=%zu\n",
				sizeof(struct fuse_init_out), sizeof(struct fuse_out_header));

			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_INIT kernel=%u.%u max_readahead=%u",
				init_data->major, init_data->minor, init_data->max_readahead);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			struct fuse_init_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));

			/*
			 * Protocol version: We speak FUSE 7.x.
			 * We MUST return major=7, and a minor <= what the kernel sent.
			 * Using a lower minor than the kernel is fine — it just means
			 * we don't support the newest features.
			 *
			 * IMPORTANT: Some kernel versions reject the INIT response if
			 * the minor version is higher than what they sent.
			 */
			out_payload.major = 7;
			out_payload.minor = init_data->minor;

			/*
			 * Keep max_readahead and max_write SMALL.
			 * Large values (like 1MB) require large read buffers
			 * and can cause EINVAL if the buffer doesn't match.
			 * For our MVP (just ls), 4KB is more than enough.
			 */
			out_payload.max_readahead = 4096;
			out_payload.flags = 0;  /* Simplest flag set */
			out_payload.max_background = 4;
			out_payload.congestion_threshold = 3;
			out_payload.max_write = 4096;
			out_payload.time_gran = 1;

			size_t reply_len = sizeof(struct fuse_out_header) + sizeof(struct fuse_init_out);

			fprintf(stderr, "[FUSE_INIT] Replying: version 7.%u, max_write=%u, total_len=%zu\n",
				out_payload.minor, out_payload.max_write, reply_len);

			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> FUSE_INIT response sent, mount is LIVE");
			fprintf(stderr, "[FUSE_INIT] Response sent successfully!\n");
			break;
		}

		/* ─── FUSE_GETATTR ─── */
		case FUSE_GETATTR: {
			if (payload_bytes < (int)sizeof(struct fuse_getattr_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_GETATTR nodeid=%lu",
				(unsigned long)header->nodeid);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			struct fuse_attr_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));

			out_payload.attr_valid = 1;
			out_payload.attr.ino   = header->nodeid;

			if (header->nodeid == FUSE_ROOT_ID) {
				out_payload.attr.mode  = 0040000 | 0755;
				out_payload.attr.nlink = 2;
			} else {
				out_payload.attr.mode  = 0100000 | 0644;
				out_payload.attr.nlink = 1;
				out_payload.attr.size  = 0;
			}

			out_payload.attr.uid     = 1000;
			out_payload.attr.gid     = 1000;
			out_payload.attr.blksize = 4096;

			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> FUSE_GETATTR response sent");
			break;
		}

		/* ─── FUSE_LOOKUP ─── */
		case FUSE_LOOKUP: {
			char name[256] = {0};
			if (payload_bytes > 0) {
				int copy_len = payload_bytes < 255 ? payload_bytes : 255;
				memcpy(name, payload, copy_len);
				name[copy_len] = '\0';
			}

			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_LOOKUP parent=%lu name=\"%s\"",
				(unsigned long)header->nodeid, name);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			send_error_reply(fuse_fd, header->unique, -ENOENT);
			ring_buffer_write(rb, 0, 0, ">> FUSE_LOOKUP -> ENOENT");
			break;
		}

		/* ─── FUSE_READDIR ─── */
		case FUSE_READDIR: {
			if (payload_bytes < (int)sizeof(struct fuse_read_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_read_in *read_data = (struct fuse_read_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc),
				"FUSE_READDIR nodeid=%lu size=%u offset=%lu",
				(unsigned long)header->nodeid, read_data->size,
				(unsigned long)read_data->offset);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			char dirbuf[4096];
			int dirbuf_len = 0;

			if (read_data->offset == 0) {
				dirbuf_len += add_dir_entry(
					dirbuf + dirbuf_len,
					(int)sizeof(dirbuf) - dirbuf_len,
					FUSE_ROOT_ID, 1, ".", FUSE_DT_DIR);
			}

			if (read_data->offset <= 1) {
				dirbuf_len += add_dir_entry(
					dirbuf + dirbuf_len,
					(int)sizeof(dirbuf) - dirbuf_len,
					FUSE_ROOT_ID, 2, "..", FUSE_DT_DIR);
			}

			if (dirbuf_len > (int)read_data->size)
				dirbuf_len = (int)read_data->size;

			send_reply(fuse_fd, header->unique, dirbuf, (size_t)dirbuf_len);

			if (dirbuf_len > 0)
				ring_buffer_write(rb, 0, 0, ">> FUSE_READDIR response (with entries)");
			else
				ring_buffer_write(rb, 0, 0, ">> FUSE_READDIR response (end of dir)");
			break;
		}

		/* ─── FUSE_READDIRPLUS ─── */
		case FUSE_READDIRPLUS: {
			ring_buffer_write(rb, header->opcode, header->nodeid, "FUSE_READDIRPLUS -> ENOSYS");
			send_error_reply(fuse_fd, header->unique, -ENOSYS);
			break;
		}

		/* ─── FUSE_OPEN / FUSE_OPENDIR ─── */
		case FUSE_OPEN:
		case FUSE_OPENDIR: {
			if (payload_bytes < (int)sizeof(struct fuse_open_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_open_in *open_data = (struct fuse_open_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "%s nodeid=%lu flags=%u",
				header->opcode == FUSE_OPEN ? "FUSE_OPEN" : "FUSE_OPENDIR",
				(unsigned long)header->nodeid, open_data->flags);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			struct fuse_open_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));
			out_payload.fh = 1;

			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> OPEN/OPENDIR response sent");
			break;
		}

		/* ─── FUSE_READ ─── */
		case FUSE_READ: {
			if (payload_bytes < (int)sizeof(struct fuse_read_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_read_in *read_data = (struct fuse_read_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc),
				"FUSE_READ nodeid=%lu size=%u offset=%lu",
				(unsigned long)header->nodeid, read_data->size,
				(unsigned long)read_data->offset);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			/* Return 0 bytes = EOF */
			send_reply(fuse_fd, header->unique, NULL, 0);
			ring_buffer_write(rb, 0, 0, ">> FUSE_READ -> EOF");
			break;
		}

		/* ─── FUSE_WRITE ─── */
		case FUSE_WRITE: {
			if (payload_bytes < (int)sizeof(struct fuse_write_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_write_in *write_data = (struct fuse_write_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc),
				"FUSE_WRITE nodeid=%lu size=%u offset=%lu",
				(unsigned long)header->nodeid, write_data->size,
				(unsigned long)write_data->offset);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			struct fuse_write_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));
			out_payload.size = write_data->size;

			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> FUSE_WRITE response sent");
			break;
		}

		/* ─── FUSE_RELEASE / FUSE_RELEASEDIR ─── */
		case FUSE_RELEASE:
		case FUSE_RELEASEDIR: {
			char desc[128];
			snprintf(desc, sizeof(desc), "%s nodeid=%lu",
				header->opcode == FUSE_RELEASE ? "FUSE_RELEASE" : "FUSE_RELEASEDIR",
				(unsigned long)header->nodeid);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			send_reply(fuse_fd, header->unique, NULL, 0);
			ring_buffer_write(rb, 0, 0, ">> RELEASE/RELEASEDIR -> OK");
			break;
		}

		/* ─── FUSE_ACCESS ─── */
		case FUSE_ACCESS: {
			if (payload_bytes < (int)sizeof(struct fuse_access_in)) {
				send_error_reply(fuse_fd, header->unique, -EINVAL);
				break;
			}

			struct fuse_access_in *acc = (struct fuse_access_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc),
				"FUSE_ACCESS nodeid=%lu mask=0x%x",
				(unsigned long)header->nodeid, acc->mask);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			send_reply(fuse_fd, header->unique, NULL, 0);
			ring_buffer_write(rb, 0, 0, ">> FUSE_ACCESS -> allowed");
			break;
		}

		/* ─── FUSE_STATFS ─── */
		case FUSE_STATFS: {
			ring_buffer_write(rb, header->opcode, header->nodeid, "FUSE_STATFS");

			struct fuse_statfs_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));
			out_payload.st.blocks  = 0;
			out_payload.st.bfree   = 0;
			out_payload.st.bavail  = 0;
			out_payload.st.files   = 1;
			out_payload.st.ffree   = 0;
			out_payload.st.bsize   = 4096;
			out_payload.st.namelen = 255;
			out_payload.st.frsize  = 4096;

			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> FUSE_STATFS response sent");
			break;
		}

		/* ─── FUSE_FORGET (no reply) ─── */
		case FUSE_FORGET: {
			ring_buffer_write(rb, header->opcode, header->nodeid,
				"FUSE_FORGET (no reply needed)");
			break;
		}

		/* ─── FUSE_BATCH_FORGET (no reply) ─── */
		case FUSE_BATCH_FORGET: {
			ring_buffer_write(rb, header->opcode, header->nodeid,
				"FUSE_BATCH_FORGET (no reply needed)");
			break;
		}

		/* ─── FUSE_DESTROY (no reply) ─── */
		case FUSE_DESTROY: {
			ring_buffer_write(rb, header->opcode, header->nodeid,
				"FUSE_DESTROY - filesystem unmounted");
			break;
		}

		/* ─── FUSE_FLUSH ─── */
		case FUSE_FLUSH: {
			ring_buffer_write(rb, header->opcode, header->nodeid, "FUSE_FLUSH");
			send_reply(fuse_fd, header->unique, NULL, 0);
			ring_buffer_write(rb, 0, 0, ">> FUSE_FLUSH -> OK");
			break;
		}

		/* ─── FUSE_FSYNC / FUSE_FSYNCDIR ─── */
		case FUSE_FSYNC:
		case FUSE_FSYNCDIR: {
			char desc[128];
			snprintf(desc, sizeof(desc), "%s nodeid=%lu",
				header->opcode == FUSE_FSYNC ? "FUSE_FSYNC" : "FUSE_FSYNCDIR",
				(unsigned long)header->nodeid);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			send_reply(fuse_fd, header->unique, NULL, 0);
			ring_buffer_write(rb, 0, 0, ">> FSYNC/FSYNCDIR -> OK");
			break;
		}

		/* ─── FUSE_SETATTR ─── */
		case FUSE_SETATTR: {
			ring_buffer_write(rb, header->opcode, header->nodeid, "FUSE_SETATTR");
			struct fuse_attr_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));
			out_payload.attr_valid = 1;
			out_payload.attr.ino   = header->nodeid;
			if (header->nodeid == FUSE_ROOT_ID) {
				out_payload.attr.mode  = 0040000 | 0755;
				out_payload.attr.nlink = 2;
			} else {
				out_payload.attr.mode  = 0100000 | 0644;
				out_payload.attr.nlink = 1;
			}
			out_payload.attr.uid     = 1000;
			out_payload.attr.gid     = 1000;
			out_payload.attr.blksize = 4096;
			send_reply(fuse_fd, header->unique, &out_payload, sizeof(out_payload));
			ring_buffer_write(rb, 0, 0, ">> FUSE_SETATTR response sent");
			break;
		}

		/* ─── FUSE_READLINK ─── */
		case FUSE_READLINK: {
			ring_buffer_write(rb, header->opcode, header->nodeid, "FUSE_READLINK");
			send_error_reply(fuse_fd, header->unique, -ENOENT);
			break;
		}

		/* ─── DEFAULT: ENOSYS ─── */
		default: {
			char desc[128];
			snprintf(desc, sizeof(desc),
				"UNHANDLED opcode=%u (%s) -> -ENOSYS",
				header->opcode, opname);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			send_error_reply(fuse_fd, header->unique, -ENOSYS);
			ring_buffer_write(rb, 0, 0, ">> unhandled -> ENOSYS");
			break;
		}
		}

		/* Advance to next message */
		offset += header->len;
	}
}
