#ifndef _USERLAND_LIB_TIME_H_
#define _USERLAND_LIB_TIME_H_

#include <types.h>

time_t time(time_t *tloc);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);

#endif /* _USERLAND_LIB_TIME_H_ */

