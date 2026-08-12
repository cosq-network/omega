#ifndef OMEGA_USER_UNISTD_H
#define OMEGA_USER_UNISTD_H

#include <stdint.h>

typedef int64_t ssize_t;

ssize_t write(int fd, const void* buffer, uint64_t count);
int close(int fd);
int sched_yield(void);
void _exit(int status);
void exit(int status);

#endif
