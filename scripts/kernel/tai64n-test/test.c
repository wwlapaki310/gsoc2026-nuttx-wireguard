/* Compile and link the actual driver allocator, not a copied model. */
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <nuttx/clock.h>
#include "wg_crypto.h"

#define EPOCH UINT64_C(0x400000000000000a)
#define THREADS 4
#define PER_THREAD 1000

static struct timespec now;
static int clock_error;
static uint8_t parallel[THREADS][PER_THREAD][WG_TAI64N_LEN];

int nxclock_gettime(clockid_t id, struct timespec *ts)
{
  assert(id == CLOCK_REALTIME);
  *ts = now;
  return clock_error;
}

static uint64_t seconds(const uint8_t *stamp)
{
  uint64_t result = 0;
  for (int i = 0; i < 8; i++)
    result = (result << 8) | stamp[i];
  return result;
}

static uint32_t nanos(const uint8_t *stamp)
{
  uint32_t result = 0;
  for (int i = 8; i < 12; i++)
    result = (result << 8) | stamp[i];
  return result;
}

static void issue(uint8_t *stamp, uint64_t sec, uint32_t nsec)
{
  assert(wg_tai64n(stamp));
  assert(seconds(stamp) == EPOCH + sec);
  assert(nanos(stamp) == nsec);
}

static void invalid(void)
{
  uint8_t stamp[WG_TAI64N_LEN], saved[WG_TAI64N_LEN];
  memset(stamp, 0xa5, sizeof(stamp));
  memcpy(saved, stamp, sizeof(saved));
  assert(!wg_tai64n(stamp));
  assert(memcmp(stamp, saved, sizeof(stamp)) == 0);
}

static void *worker(void *arg)
{
  size_t index = (size_t)arg;
  for (int i = 0; i < PER_THREAD; i++)
    {
      assert(wg_tai64n(parallel[index][i]));
      if (i > 0)
        assert(memcmp(parallel[index][i - 1], parallel[index][i], WG_TAI64N_LEN) < 0);
    }
  return NULL;
}

static int compare(const void *a, const void *b)
{
  return memcmp(a, b, WG_TAI64N_LEN);
}

int main(void)
{
  uint8_t stamp[WG_TAI64N_LEN];
  pthread_t threads[THREADS];

  now.tv_sec = 100;
  now.tv_nsec = 123456789;
  issue(stamp, 100, 120000000);  /* Endianness and tick truncation. */
  issue(stamp, 100, 130000000);  /* Same tick must increase. */
  now.tv_sec = 50;
  issue(stamp, 100, 140000000);  /* Valid realtime rollback. */

  clock_error = -EIO;
  invalid();
  clock_error = 0;
  now.tv_sec = -1;
  invalid();
  now.tv_sec = 50;
  now.tv_nsec = -1;
  invalid();
  now.tv_nsec = NSEC_PER_SEC;
  invalid();
  now.tv_nsec = 0;
  issue(stamp, 100, 150000000);  /* Errors did not consume state. */

  now.tv_sec = 101;
  now.tv_nsec = 999999999;
  issue(stamp, 101, 990000000);
  issue(stamp, 102, 0);          /* Nanosecond carry. */
  now.tv_sec = 1000;
  now.tv_nsec = 0;
  issue(stamp, 1000, 0);        /* Forward jump follows realtime. */

  for (size_t i = 0; i < THREADS; i++)
    assert(pthread_create(&threads[i], NULL, worker, (void *)i) == 0);
  for (int i = 0; i < THREADS; i++)
    assert(pthread_join(threads[i], NULL) == 0);

  qsort(parallel, THREADS * PER_THREAD, WG_TAI64N_LEN, compare);
  const uint8_t *flat = (const uint8_t *)parallel;
  for (int i = 0; i < THREADS * PER_THREAD; i++)
    {
      const uint8_t *item = flat + i * WG_TAI64N_LEN;
      assert(nanos(item) < NSEC_PER_SEC);
      assert(nanos(item) % NSEC_PER_TICK == 0);
      if (i > 0)
        assert(memcmp(item - WG_TAI64N_LEN, item, WG_TAI64N_LEN) < 0);
    }

  puts("PASS: encoding, repeat/rollback/carry, forward jump, failure atomicity, 4000 concurrent allocations");
  return 0;
}
