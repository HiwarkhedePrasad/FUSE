#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int mount_fuse(const char *mountpoint) {
	int fds[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0) {
		perror("[MOUNT] socketpair failed");
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		perror("[MOUNT] fork failed");
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	if (pid == 0) {
		/* ── Child process ── */
		close(fds[1]);

		char env[64];
		snprintf(env, sizeof(env), "_FUSE_COMMFD=%d", fds[0]);
		putenv(env);

		/*
		 * fusermount3 will:
		 * 1. Open /dev/fuse
		 * 2. Mount the filesystem
		 * 3. Send the FUSE fd back to us via the socket
		 * 4. Keep running (holds the mount)
		 */
		execlp("fusermount3", "fusermount3", "-o", "rw,nosuid,nodev", mountpoint, NULL);
		perror("[MOUNT] execlp fusermount3 failed");
		_exit(1);
	}

	/* ── Parent process ── */
	close(fds[0]);

	/*
	 * Wait for fusermount3 to send us the FUSE fd via SCM_RIGHTS.
	 * This uses recvmsg() with ancillary data.
	 */
	struct msghdr msg = {0};
	char buf[256];
	struct iovec io = { .iov_base = buf, .iov_len = sizeof(buf) };
	msg.msg_iov = &io;
	msg.msg_iovlen = 1;

	char cmsg_buf[CMSG_SPACE(sizeof(int))];
	msg.msg_control = cmsg_buf;
	msg.msg_controllen = sizeof(cmsg_buf);

	fprintf(stderr, "[MOUNT] Waiting for FUSE fd from fusermount3...\n");

	ssize_t recv_len = recvmsg(fds[1], &msg, 0);
	if (recv_len < 0) {
		fprintf(stderr, "[MOUNT] recvmsg failed: %s\n", strerror(errno));

		/* Check if the child process is still alive */
		int status;
		pid_t result = waitpid(pid, &status, WNOHANG);
		if (result == pid) {
			if (WIFEXITED(status)) {
				fprintf(stderr, "[MOUNT] fusermount3 exited with code %d\n", WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				fprintf(stderr, "[MOUNT] fusermount3 killed by signal %d\n", WTERMSIG(status));
			}
		} else {
			fprintf(stderr, "[MOUNT] fusermount3 may still be running (pid=%d)\n", pid);
		}

		close(fds[1]);
		return -1;
	}

	fprintf(stderr, "[MOUNT] recvmsg returned %zd bytes\n", recv_len);

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg == NULL) {
		fprintf(stderr, "[MOUNT] No ancillary data received from fusermount3\n");
		fprintf(stderr, "[MOUNT] msg_controllen=%zu, msg_flags=%d\n",
			msg.msg_controllen, msg.msg_flags);
		close(fds[1]);
		return -1;
	}

	if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) {
		fprintf(stderr, "[MOUNT] Unexpected cmsg: level=%d type=%d (expected SOL_SOCKET/SCM_RIGHTS)\n",
			cmsg->cmsg_level, cmsg->cmsg_type);
		close(fds[1]);
		return -1;
	}

	int fuse_fd;
	memcpy(&fuse_fd, CMSG_DATA(cmsg), sizeof(int));

	fprintf(stderr, "[MOUNT] Received FUSE fd=%d from fusermount3\n", fuse_fd);
	close(fds[1]);

	/* Verify the fd is valid */
	int fl = fcntl(fuse_fd, F_GETFL);
	if (fl < 0) {
		fprintf(stderr, "[MOUNT] FUSE fd %d is invalid: %s\n", fuse_fd, strerror(errno));
		close(fuse_fd);
		return -1;
	}
	fprintf(stderr, "[MOUNT] FUSE fd %d is valid (flags=0x%x)\n", fuse_fd, fl);

	return fuse_fd;
}