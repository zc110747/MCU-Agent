/*
 * cc.h - LwIP port "compiler/CPU" header (NO_SYS / GCC / newlib-nano).
 *
 * This intentionally shadows third_party/LwIP/system/arch/cc.h so that we
 * do NOT pull in <sys/time.h> (which newlib-nano may not provide cleanly)
 * and keep the build self-contained for bare-metal.
 */
#ifndef __CC_H__
#define __CC_H__

#include "cpu.h"
#include <stdlib.h>
#include <stdio.h>

typedef int sys_prot_t;

#define LWIP_PROVIDE_ERRNO

/* Define our own struct timeval (do NOT include <sys/time.h>). */
#define LWIP_TIMEVAL_PRIVATE 1

#if defined (__GNUC__)

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__ ((__packed__))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#endif

#define LWIP_PLATFORM_ASSERT(x) do {printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     x, __LINE__, __FILE__); } while(0)

/* Random number generator (used for TCP ISS / ports). */
#define LWIP_RAND() ((u32_t)rand())

#endif /* __CC_H__ */
