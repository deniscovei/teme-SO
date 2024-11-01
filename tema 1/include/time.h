#ifndef __TIME_H__
#define __TIME_H__	1

typedef long time_t;

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

int nanosleep(const struct timespec *t1, struct timespec *t2);
unsigned int sleep(unsigned int seconds);

#endif
