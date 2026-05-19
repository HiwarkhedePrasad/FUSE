#include <unistd.h>

#include "fuse_proto.h"
#include "decoder_api.h"

void intercept_fuse_loop(int fuse_fd) {
	char buffer[8192];
	int bytes = (int)read(fuse_fd, buffer, sizeof(buffer));

	if (bytes > 0) {
		decode_fuse_message(buffer, bytes);
	}
}

