#include <unistd.h>
#include <internal/syscall.h>
#include <errno.h>
#include <internal/types.h>
#include <time.h>

int nanosleep(const struct timespec *t1, struct timespec *t2)
{
	int ret = syscall(__NR_nanosleep, t1, t2);

	if (ret < 0) {
		errno = -ret;
		return -1;
	}

	return ret;
}

unsigned int sleep(unsigned int seconds)
{
	struct timespec remaining, request = { seconds, 0 };

	return nanosleep(&request, &remaining);
}
