/* Deterministic replacement for the NuttX clock API. */
#ifndef WG_TEST_CLOCK_H
#define WG_TEST_CLOCK_H
#include <time.h>
#define NSEC_PER_SEC 1000000000L
#define NSEC_PER_TICK 10000000L
int nxclock_gettime(clockid_t id, struct timespec *ts);
#endif
