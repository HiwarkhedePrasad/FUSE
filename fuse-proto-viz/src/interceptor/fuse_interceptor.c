#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "decoder_api.h"
#include "ring_buffer.h"

static struct ring_buffer g_ring_buffer;

static const char *MOUNT_DIR = "./my_mnt_dir";

int mount_fuse(const char *mountpoint);

static void handle_sigint(int sig) {
	(void)sig;
	printf("\n[EXIT] Caught SIGINT. Unmounting %s...\n", MOUNT_DIR);
	fflush(stdout);

	char cmd[256];
	snprintf(cmd, sizeof(cmd), "fusermount3 -u %s", MOUNT_DIR);
	system(cmd);
	exit(0);
}

static void *consumer_thread(void *arg) {
	(void)arg;
	struct fuse_event event;
	while (1) {
		if (ring_buffer_read(&g_ring_buffer, &event)) {
			printf("{\"ts\":%lu,\"op\":%u,\"node\":%lu,\"desc\":\"%s\"}\n",
				(unsigned long)event.timestamp,
				event.opcode,
				(unsigned long)event.nodeid,
				event.description);
			fflush(stdout);
		} else {
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
			nanosleep(&ts, NULL);
		}
	}
	return NULL;
}

int main(void) {
	signal(SIGINT, handle_sigint);

	ring_buffer_init(&g_ring_buffer);
	pthread_t consumer;
	if (pthread_create(&consumer, NULL, consumer_thread, NULL) != 0) {
		perror("pthread_create failed");
		return 1;
	}
	pthread_detach(consumer);

	/* ── Clean up any stale mount ── */
	printf("[INIT] Cleaning up old mounts...\n"); fflush(stdout);
	system("fusermount3 -u ./my_mnt_dir 2>/dev/null");
	usleep(200000);  /* 200ms settle time */

	/* ── Check /dev/fuse ── */
	if (access("/dev/fuse", R_OK | W_OK) != 0) {
		printf("[ERROR] Cannot access /dev/fuse: %s\n", strerror(errno));
		printf("  Fix: sudo modprobe fuse && sudo usermod -aG fuse $USER\n");
		fflush(stdout);
		return 1;
	}
	printf("[INIT] /dev/fuse accessible\n"); fflush(stdout);

	/* ── Check fusermount3 ── */
	int rc = system("which fusermount3 >/dev/null 2>&1");
	if (rc != 0) {
		printf("[ERROR] fusermount3 not found in PATH\n");
		printf("  Fix: sudo apt install fuse3\n");
		fflush(stdout);
		return 1;
	}
	printf("[INIT] fusermount3 found\n"); fflush(stdout);

	/* ── Ensure mount point exists ── */
	mkdir(MOUNT_DIR, 0777);

	/* ── Mount ── */
	printf("[INIT] Mounting FUSE on %s...\n", MOUNT_DIR); fflush(stdout);
	int fuse_fd = mount_fuse(MOUNT_DIR);
	if (fuse_fd < 0) {
		printf("[ERROR] mount_fuse() returned %d\n", fuse_fd);
		printf("  Possible fixes:\n");
		printf("  1. Make sure %s is an empty directory\n", MOUNT_DIR);
		printf("  2. Run: fusermount3 -u %s (clear stale mount)\n", MOUNT_DIR);
		printf("  3. Check kernel log: dmesg | tail -5\n");
		fflush(stdout);
		return 1;
	}

	printf("[INIT] FUSE mounted! fd=%d\n", fuse_fd); fflush(stdout);

	/* ── Verify fd is valid ── */
	{
		int fl = fcntl(fuse_fd, F_GETFL);
		if (fl < 0) {
			printf("[ERROR] FUSE fd %d is invalid: %s\n", fuse_fd, strerror(errno));
			fflush(stdout);
			return 1;
		}
		printf("[INIT] fd flags: 0x%x\n", fl); fflush(stdout);
	}

	/*
	 * DO NOT set O_NONBLOCK on the FUSE fd!
	 * DO NOT use epoll with the FUSE fd!
	 *
	 * Some Linux kernel versions (especially 5.x/6.x with FUSE3)
	 * return EINVAL from read() when the fd is in non-blocking mode
	 * or when used with epoll in certain configurations.
	 *
	 * The simplest, most reliable approach is a blocking read() loop.
	 * This is what libfuse3 does internally.
	 */

	/* ── Allocate read buffer ── */
	/*
	 * Buffer must be >= FUSE_MIN_READ_BUFFER (8192).
	 * We use 128KB which is plenty for all FUSE operations
	 * given our small max_write (4096).
	 */
	size_t buf_size = 131072;
	char *buffer = (char *)malloc(buf_size);
	if (!buffer) {
		perror("malloc failed");
		return 1;
	}

	printf("[INIT] Ready. Run 'ls %s' in another terminal.\n", MOUNT_DIR);
	printf("[INIT] Press Ctrl+C to exit.\n");
	fflush(stdout);

	/* ══════════════════════════════════════════════════════════════
	 *  Main loop: simple blocking read()
	 *
	 *  No epoll.  No O_NONBLOCK.  Just blocking I/O.
	 *  read() will block until the kernel sends a FUSE request,
	 *  then we decode and respond.
	 * ══════════════════════════════════════════════════════════════ */
	int einval_count = 0;

	while (1) {
		errno = 0;
		int bytes = (int)read(fuse_fd, buffer, buf_size);

		if (bytes > 0) {
			/* Success — decode the message(s) */
			einval_count = 0;
			decode_fuse_message(buffer, bytes, fuse_fd, &g_ring_buffer);

		} else if (bytes == 0) {
			/* EOF — device was closed (unmounted) */
			printf("[FUSE] Device closed (unmounted)\n"); fflush(stdout);
			break;

		} else {
			/* read() returned -1 */
			int saved_errno = errno;

			if (saved_errno == ENODEV) {
				printf("[FUSE] Device unmounted (ENODEV)\n"); fflush(stdout);
				break;
			}

			if (saved_errno == EINTR) {
				/* Interrupted by signal — just retry */
				continue;
			}

			if (saved_errno == EINVAL) {
				einval_count++;
				if (einval_count == 1) {
					/*
					 * First EINVAL — the FUSE connection is broken.
					 * This typically means:
					 *   1. FUSE_INIT response was rejected by the kernel
					 *   2. The mount was aborted
					 *   3. A previous stale mount is interfering
					 */
					printf("\n[ERROR] read() returned EINVAL on FUSE fd %d\n", fuse_fd);
					printf("  This means the FUSE connection is broken.\n");
					printf("  Possible causes:\n");
					printf("    - Stale mount: try 'fusermount3 -u %s' then restart\n", MOUNT_DIR);
					printf("    - Wrong FUSE protocol version\n");
					printf("    - Kernel rejected FUSE_INIT response\n");
					printf("  Run 'dmesg | grep fuse' for kernel-side errors.\n\n");
					fflush(stdout);
				}

				if (einval_count >= 5) {
					printf("[FUSE] Giving up after %d EINVAL errors.\n", einval_count);
					fflush(stdout);
					break;
				}

				/* Back off to avoid spinning */
				sleep(1);
				continue;
			}

			/* Other errors */
			printf("[FUSE] read() error: %s (errno=%d)\n", strerror(saved_errno), saved_errno);
			fflush(stdout);

			if (saved_errno == EAGAIN || saved_errno == EWOULDBLOCK) {
				/* Shouldn't happen without O_NONBLOCK, but handle it */
				usleep(10000);  /* 10ms */
				continue;
			}

			/* Unknown error — bail out */
			break;
		}
	}

	free(buffer);
	close(fuse_fd);

	/* Clean up mount on exit */
	system("fusermount3 -u ./my_mnt_dir 2>/dev/null");

	printf("[FUSE] Exiting.\n"); fflush(stdout);
	return 0;
}
