/**
  ******************************************************************************
  * @file    snmp_msg.h
  * @brief   SNMP v2c message decode + dispatch + response encode.
  *
  *          Pure logic (only depends on ber.c + mib.c). Kept separate from
  *          snmp_agent.c so it can be unit-tested on the PC without lwIP.
  ******************************************************************************
  */
#ifndef __SNMP_MSG_H__
#define __SNMP_MSG_H__

#include <stdint.h>
#include "ber.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Parse a request, dispatch each varbind via the MIB, build the response into
 * out[] (must be >= outcap). Returns response length (>0) or <0 on fatal parse
 * error (caller should then send nothing — e.g. wrong community). */
int handle_message(const uint8_t *in, uint32_t inlen,
                   uint8_t *out, uint32_t outcap);

#ifdef __cplusplus
}
#endif

#endif /* __SNMP_MSG_H__ */
