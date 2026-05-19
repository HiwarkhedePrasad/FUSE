#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>

#include "fuse_structs.h"
#include "decoder_api.h"
#include "ring_buffer.h"

void decode_fuse_message(char *raw_buffer, int bytes_read, int fuse_fd, struct ring_buffer *rb) {
	if (bytes_read < (int)sizeof(struct fuse_in_header)) {
		printf("Error: Message too short to be a valid FUSE request.\n");
		return;
	}

	struct fuse_in_header *header = (struct fuse_in_header *)raw_buffer;

	char _top_desc[128];
	snprintf(_top_desc, sizeof(_top_desc),
		"opcode=%u nodeid=%lu len=%u", header->opcode, (unsigned long)header->nodeid, header->len);
	ring_buffer_write(rb, header->opcode, header->nodeid, _top_desc);

	char *payload = raw_buffer + sizeof(struct fuse_in_header);
	int payload_bytes = bytes_read - (int)sizeof(struct fuse_in_header);

	switch (header->opcode) {
		case FUSE_INIT: {
			if (payload_bytes < (int)sizeof(struct fuse_init_in)) {
				printf("Error: Short FUSE_INIT payload.\n");
				return;
			}

			struct fuse_init_in *init_data = (struct fuse_init_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_INIT kernel=%u.%u", init_data->major, init_data->minor);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			struct fuse_init_out out_payload;
			memset(&out_payload, 0, sizeof(out_payload));
			out_payload.major = 7;
			out_payload.minor = init_data->minor; // Tell the kernel we speak its exact dialect
			out_payload.max_readahead = init_data->max_readahead;
			out_payload.max_write = 1048576;

			struct fuse_out_header out_header;
			out_header.len = sizeof(struct fuse_out_header) + sizeof(struct fuse_init_out);
			out_header.error = 0;
			out_header.unique = header->unique;

			struct iovec iov[2];
			iov[0].iov_base = &out_header;
			iov[0].iov_len = sizeof(out_header);
			iov[1].iov_base = &out_payload;
			iov[1].iov_len = sizeof(out_payload);

			if (writev(fuse_fd, iov, 2) < 0) {
				perror("writev failed");
			}
			ring_buffer_write(rb, 0, 0, "sent FUSE_INIT response, kernel unlocked");
			break;
		}

		case FUSE_READ:
		case FUSE_READDIR: {
			if (payload_bytes < (int)sizeof(struct fuse_read_in)) {
				printf("Error: Short FUSE_READ payload.\n");
				return;
			}

			struct fuse_read_in *read_data = (struct fuse_read_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "%s size=%u offset=%lu",
				header->opcode == FUSE_READ ? "FUSE_READ" : "FUSE_READDIR",
				read_data->size, (unsigned long)read_data->offset);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}

		case FUSE_GETATTR: {
			if (payload_bytes < (int)sizeof(struct fuse_getattr_in)) {
				printf("Error: Short FUSE_GETATTR payload.\n");
				return;
			}

			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_GETATTR nodeid=%lu", (unsigned long)header->nodeid);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);

			// 1. Build the payload
			struct fuse_attr_out out_payload;
			memset(&out_payload, 0, sizeof(struct fuse_attr_out));
			
			out_payload.attr_valid = 1; // Tell kernel to cache this for 1 second
			out_payload.attr.ino = header->nodeid; 
			
			// MAGIC TRICK: 0040000 means "Directory", 0755 means "rwxr-xr-x" permissions
			out_payload.attr.mode = 0040000 | 0755; 
			
			out_payload.attr.nlink = 2;
			out_payload.attr.uid = 1000; // Your standard Linux User ID
			out_payload.attr.gid = 1000;

			// 2. Build the header
			struct fuse_out_header out_header;
			out_header.len = sizeof(struct fuse_out_header) + sizeof(struct fuse_attr_out);
			out_header.error = 0; // Success!
			out_header.unique = header->unique; // Match the kernel's request!

			// 3. Send it back
			struct iovec iov[2];
			iov[0].iov_base = &out_header;
			iov[0].iov_len = sizeof(out_header);
			iov[1].iov_base = &out_payload;
			iov[1].iov_len = sizeof(out_payload);

			writev(fuse_fd, iov, 2);
			ring_buffer_write(rb, 0, 0, "sent FUSE_GETATTR response, told kernel it is a directory");
			break;
		}

		case FUSE_OPEN: {
			if (payload_bytes < (int)sizeof(struct fuse_open_in)) {
				printf("Error: Short FUSE_OPEN payload.\n");
				return;
			}

			struct fuse_open_in *open_data = (struct fuse_open_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_OPEN flags=%u open_flags=%u",
				open_data->flags, open_data->open_flags);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}

		case FUSE_WRITE: {
			if (payload_bytes < (int)sizeof(struct fuse_write_in)) {
				printf("Error: Short FUSE_WRITE payload.\n");
				return;
			}

			struct fuse_write_in *write_data = (struct fuse_write_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_WRITE size=%u offset=%lu",
				write_data->size, (unsigned long)write_data->offset);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}

		case FUSE_RELEASE: {
			if (payload_bytes < (int)sizeof(struct fuse_release_in)) {
				printf("Error: Short FUSE_RELEASE payload.\n");
				return;
			}

			struct fuse_release_in *release_data = (struct fuse_release_in *)payload;
			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_RELEASE fh=%lu", (unsigned long)release_data->fh);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}

		case FUSE_LOOKUP: {
			char desc[128];
			snprintf(desc, sizeof(desc), "FUSE_LOOKUP name=%s", payload);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}

		default: {
			char desc[128];
			snprintf(desc, sizeof(desc), "unhandled opcode=%u", header->opcode);
			ring_buffer_write(rb, header->opcode, header->nodeid, desc);
			break;
		}
	}
}
