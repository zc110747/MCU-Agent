/**
 * Minimal stand-in for the firmware's main.h.
 *
 * Only the two things the font stack actually uses live here: the result type
 * and its two values.
 */
#ifndef HOST_SHIM_MAIN_H
#define HOST_SHIM_MAIN_H

#include <stdint.h>

typedef enum
{
    RT_OK = 0,
    RT_FAIL,
} GlobalType_t;

#endif /* HOST_SHIM_MAIN_H */
