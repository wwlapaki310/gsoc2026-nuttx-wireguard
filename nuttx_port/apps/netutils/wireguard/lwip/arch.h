/**
 * lwip/arch.h compatibility shim for NuttX
 *
 * Maps lwIP basic integer types to stdint.h equivalents.
 * NuttX does not use upstream lwIP, so these types are not defined anywhere
 * in the NuttX include tree.
 */

#ifndef LWIP_ARCH_H_NUTTX_COMPAT
#define LWIP_ARCH_H_NUTTX_COMPAT

#include <stdint.h>

typedef uint8_t  u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int8_t   s8_t;
typedef int16_t  s16_t;
typedef int32_t  s32_t;

#endif /* LWIP_ARCH_H_NUTTX_COMPAT */
