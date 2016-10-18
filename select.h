#pragma once
#include <stdbool.h>
#include <sys/time.h>

enum select_event_mask
{
	sem_read = 4,
	sem_write = 2,
	sem_error = 1,
	sem_fail = 8,
	sem_all = sem_read | sem_write | sem_error
};

enum select_event_mask select_single(int fd, enum select_event_mask mask);
bool select_multiple(int count, int *fd, enum select_event_mask *mask);

enum select_event_mask select_single_t(int fd, enum select_event_mask mask, struct timeval *timeout);
bool select_multiple_t(int count, int *fd, enum select_event_mask *mask, struct timeval *timeout);
