/**
 ******************************************************************************
 * @file    ov5640_ref.h
 * @brief   OV5640 bring-up sequence ported from a known-good STM32H7 project.
 ******************************************************************************
 */
#ifndef OV5640_REF_H
#define OV5640_REF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Reset the sensor, push the verified register table (YUV422/YUYV output) and
 * set the DVP output size. Returns 0 on success, -1 if any write or read-back
 * verification failed - inspect ov5640_ref_fail_* for the offending entry. */
int32_t ov5640_ref_init(void);

/* Diagnostics for the read-back verification inside ov5640_ref_init(). */
extern volatile int32_t  ov5640_ref_fail_index; /* -1 when the table verified */
extern volatile uint16_t ov5640_ref_fail_reg;
extern volatile uint8_t  ov5640_ref_fail_want;
extern volatile uint8_t  ov5640_ref_fail_got;

#ifdef __cplusplus
}
#endif
#endif /* OV5640_REF_H */
