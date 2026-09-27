/* Host test shim, not part of the NuttX build. */
#ifndef WG_TEST_CONFIG_H
#define WG_TEST_CONFIG_H
#define FAR

/* Mirrors the Kconfig default. Without an RTC, NuttX seeds CLOCK_REALTIME
 * from this year, which is how wg_tai64n() recognises a clock that was never
 * set. Keep it defined here so the allocator is compiled with the same
 * check the target build gets.
 */

#define CONFIG_START_YEAR 2018
#endif
