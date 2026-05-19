#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>

#include "fuse_structs.h"
#include "decoder_api.h"

void decode_fuse_message(char *raw_buffer, int bytes_read, int fuse_fd) {
	if (bytes_read < (int)sizeof(struct fuse_in_header)) {
		printf("Error: Message too short to be a valid FUSE request.\n");
		return;
	}

	struct fuse_in_header *header = (struct fuse_in_header *)raw_buffer;

	printf("\n=== NEW KERNEL REQUEST ===\n");
	printf("Total Length: %u bytes\n", header->len);
	printf("Opcode: %u\n", header->opcode);
	printf("Target NodeID: %" PRIu64 "\n", header->nodeid);

	char *payload = raw_buffer + sizeof(struct fuse_in_header);
	int payload_bytes = bytes_read - (int)sizeof(struct fuse_in_header);

	switch (header->opcode) {
		case FUSE_INIT: {
			if (payload_bytes < (int)sizeof(struct fuse_init_in)) {
				printf("Error: Short FUSE_INIT payload.\n");
				return;
			}

			struct fuse_init_in *init_data = (struct fuse_init_in *)payload;
			printf("[+] Operation: FUSE_INIT\n");
			printf("    Kernel Protocol Version: %u.%u\n", init_data->major, init_data->minor);

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
			} else {
				printf("    [->] Sent FUSE_INIT response! Kernel unlocked.\n");
			}
			break;
		}

		case FUSE_READ:
		case FUSE_READDIR: {
			if (payload_bytes < (int)sizeof(struct fuse_read_in)) {
				printf("Error: Short FUSE_READ payload.\n");
				return;
			}

			struct fuse_read_in *read_data = (struct fuse_read_in *)payload;
			printf("[+] Operation: %s\n", header->opcode == FUSE_READ ? "FUSE_READ" : "FUSE_READDIR");
			printf("    Reading %u bytes starting at offset %" PRIu64 "\n", read_data->size, read_data->offset);
			break;
		}

		case FUSE_GETATTR: {
			if (payload_bytes < (int)sizeof(struct fuse_getattr_in)) {
				printf("Error: Short FUSE_GETATTR payload.\n");
				return;
			}

			printf("[+] Operation: FUSE_GETATTR\n");
			printf("    Kernel is asking for attributes of NodeID: %lu\n", header->nodeid);

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
			printf("    [->] Sent FUSE_GETATTR response! Told kernel it's a directory.\n");
			break;
		}

		case FUSE_OPEN: {
			if (payload_bytes < (int)sizeof(struct fuse_open_in)) {
				printf("Error: Short FUSE_OPEN payload.\n");
				return;
			}

			struct fuse_open_in *open_data = (struct fuse_open_in *)payload;
			printf("[+] Operation: FUSE_OPEN\n");
			printf("    flags: %u\n", open_data->flags);
			printf("    open_flags: %u\n", open_data->open_flags);
			break;
		}

		case FUSE_WRITE: {
			if (payload_bytes < (int)sizeof(struct fuse_write_in)) {
				printf("Error: Short FUSE_WRITE payload.\n");
				return;
			}

			struct fuse_write_in *write_data = (struct fuse_write_in *)payload;
			printf("[+] Operation: FUSE_WRITE\n");
			printf("    Writing %u bytes starting at offset %" PRIu64 "\n", write_data->size, write_data->offset);
			break;
		}

		case FUSE_RELEASE: {
			if (payload_bytes < (int)sizeof(struct fuse_release_in)) {
				printf("Error: Short FUSE_RELEASE payload.\n");
				return;
			}

			struct fuse_release_in *release_data = (struct fuse_release_in *)payload;
			printf("[+] Operation: FUSE_RELEASE\n");
			printf("    fh: %" PRIu64 "\n", release_data->fh);
			break;
		}

		case FUSE_LOOKUP:
			printf("[+] Operation: FUSE_LOOKUP\n");
			printf("    Looking up file name: %s\n", payload);
			break;

		default:
			printf("[-] Unhandled Opcode: %u\n", header->opcode);
			break;
	}
}

