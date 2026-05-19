#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "decoder_api.h"
#include "ring_buffer.h"

static struct ring_buffer g_ring_buffer;

#define MAX_EVENTS 10

static const char *MOUNT_DIR = "./my_mnt_dir";

int mount_fuse(const char *mountpoint);

static void handle_sigint(int sig) {
	(void)sig;
	printf("\nCaught SIGINT. Unmounting %s...\n", MOUNT_DIR);

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
			struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000 };
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

    system("fusermount3 -u ./my_mnt_dir 2>/dev/null");

    
	mkdir(MOUNT_DIR, 0777);

	printf("Mounting FUSE on %s...\n", MOUNT_DIR);
	int fuse_fd = mount_fuse(MOUNT_DIR);
	if (fuse_fd < 0) {
		printf("Failed to mount FUSE. Exiting.\n");
		return 1;
	}

	int flags = fcntl(fuse_fd, F_GETFL, 0);
	if (flags >= 0) {
		fcntl(fuse_fd, F_SETFL, flags | O_NONBLOCK);
	}

	int epoll_fd = epoll_create1(0);
	if (epoll_fd < 0) {
		perror("epoll_create1 failed");
		return 1;
	}

	struct epoll_event ev;
	struct epoll_event events[MAX_EVENTS];
	ev.events = EPOLLIN;
	ev.data.fd = fuse_fd;

	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fuse_fd, &ev) < 0) {
		perror("epoll_ctl failed");
		close(epoll_fd);
		close(fuse_fd);
		return 1;
	}

	printf("Listening for kernel messages. Run 'ls %s' in another terminal.\n", MOUNT_DIR);
	printf("Press Ctrl+C to exit.\n");

	char buffer[8192];

	while (1) {
		int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (nfds < 0) {
			perror("epoll_wait failed");
			continue;
		}

		for (int i = 0; i < nfds; i++) {
			if (events[i].data.fd == fuse_fd) {
				int bytes = (int)read(fuse_fd, buffer, sizeof(buffer));
				if (bytes > 0) {
						decode_fuse_message(buffer, bytes, fuse_fd, &g_ring_buffer);
				}
			}
		}
	}

	return 0;
}
