#include <unistd.h>
#include <sys/select.h>
#include <errno.h>
#include "select.h"

enum select_event_mask select_single(int fd, enum select_event_mask mask)
{
	return select_single_t(fd, mask, NULL);
}

bool select_multiple(int count, int *fd, enum select_event_mask *mask)
{
	return select_multiple_t(count, fd, mask, NULL);
}

enum select_event_mask select_single_t(int fd, enum select_event_mask mask, struct timeval *timeout)
{
	fd_set fds_r;
	fd_set fds_w;
	fd_set fds_e;
	int res;
	do {
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_ZERO(&fds_e);
		if (mask & sem_read) {
			FD_SET(fd, &fds_r);
		}
		if (mask & sem_write) {
			FD_SET(fd, &fds_w);
		}
		if (mask & sem_error) {
			FD_SET(fd, &fds_e);
		}
		res = select(fd + 1, &fds_r, &fds_w, &fds_e, timeout);
		if (res == -1 && errno != EINTR) {
			return sem_fail;
		}
	} while (res <= 0 && !(res == 0 && timeout != NULL));
	res = 0;
	if (FD_ISSET(fd, &fds_r)) {
		res |= sem_read;
	}
	if (FD_ISSET(fd, &fds_w)) {
		res |= sem_write;
	}
	if (FD_ISSET(fd, &fds_e)) {
		res |= sem_error;
	}
	return res;
}

bool select_multiple_t(int count, int *fd, enum select_event_mask *mask, struct timeval *timeout)
{
	fd_set fds_r;
	fd_set fds_w;
	fd_set fds_e;
	int res;
	int max = 0;
	do {
		FD_ZERO(&fds_r);
		FD_ZERO(&fds_w);
		FD_ZERO(&fds_e);
		for (int i = 0; i < count; i++) {
			if (fd[i] > max) {
				max = fd[i];
			}
			if (mask[i] & sem_read) {
				FD_SET(fd[i], &fds_r);
			}
			if (mask[i] & sem_write) {
				FD_SET(fd[i], &fds_w);
			}
			if (mask[i] & sem_error) {
				FD_SET(fd[i], &fds_e);
			}
		}
		res = select(max + 1, &fds_r, &fds_w, &fds_e, timeout);
		if (res == -1 && errno != EINTR) {
			return false;
		}
	} while (res <= 0 && !(res == 0 && timeout != NULL));
	for (int i = 0; i < count; i++) {
		enum select_event_mask res = 0;
		if (FD_ISSET(fd[i], &fds_r)) {
			res |= sem_read;
		}
		if (FD_ISSET(fd[i], &fds_w)) {
			res |= sem_write;
		}
		if (FD_ISSET(fd[i], &fds_e)) {
			res |= sem_error;
		}
		mask[i] = res;
	}
	return true;
}
