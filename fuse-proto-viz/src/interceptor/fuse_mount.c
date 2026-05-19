#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int mount_fuse(const char *mountpoint) {
	int fds[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
		perror("socketpair failed");
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("fork failed");
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0) {
		close(fds[1]);

		char env[64];
		snprintf(env, sizeof(env), "_FUSE_COMMFD=%d", fds[0]);
		putenv(env);

		execlp("fusermount3", "fusermount3", "-o", "rw,nosuid,nodev", mountpoint, NULL);
		perror("execlp fusermount3 failed");
		_exit(1);
	}

	close(fds[0]);

	struct msghdr msg = {0};
	char buf[256];
	struct iovec io = { .iov_base = buf, .iov_len = sizeof(buf) };
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;

	char cmsg_buf[CMSG_SPACE(sizeof(int))];
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	if (recvmsg(fds[1], &msg, 0) < 0) {
		perror("recvmsg failed");
		close(fds[1]);
		return -1;
	}

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg != NULL && cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
		int fuse_fd;
		memcpy(&fuse_fd, CMSG_DATA(cmsg), sizeof(int));
		close(fds[1]);
		return fuse_fd;
	}

	printf("Failed to receive file descriptor via SCM_RIGHTS.\n");
	close(fds[1]);
	return -1;
}

