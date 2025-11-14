#include <time.h>
#include <syscall.h>

time_t time(time_t *tloc) {
    time_t result = (time_t)syscall0(SYS_TIME);
    if (tloc) {
        *tloc = result;
    }
    return result;
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    if (!req) {
        return -1;
    }

    if (req->tv_nsec >= 1000000000u) {
        if (rem) {
            rem->tv_sec = req->tv_sec;
            rem->tv_nsec = req->tv_nsec;
        }
        return -1;
    }

    uint32_t ret = syscall2(SYS_NANOSLEEP, (uint32_t)req, (uint32_t)rem);
    if (ret == (uint32_t)-1) {
        return -1;
    }
    return 0;
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = {
        .tv_sec = seconds,
        .tv_nsec = 0,
    };
    struct timespec rem = req;

    if (nanosleep(&req, &rem) == 0) {
        return 0;
    }

    return rem.tv_sec;
}

