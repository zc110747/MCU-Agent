/* ---------------------------------------------------------------------------
 * Byte FIFO shared between an interrupt producer/consumer and the main loop.
 *
 * Capacity is a power of two so the index wrap is a single AND.
 *
 * Concurrency model: exactly ONE producer and ONE consumer at any time, but
 * they may run on different contexts (main loop vs ISR). head/tail are plain
 * 32-bit values written by only one side each, which is enough on Cortex-M7
 * for the *data*; the counter updates are additionally wrapped in short
 * PRIMASK critical sections so an ISR can never observe a half-updated pair.
 * -------------------------------------------------------------------------*/
#ifndef RINGBUF_H_
#define RINGBUF_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cmsis_compiler.h"      /* __get_PRIMASK() / __disable_irq() */

/* Enter/leave a short interrupt-disabled section (safe to nest). */
static inline uint32_t rb_lock(void) {
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}
static inline void rb_unlock(uint32_t primask) {
  __set_PRIMASK(primask);
}

typedef struct {
  uint8_t  *buf;
  uint32_t  cap;        /* power of two                     */
  uint32_t  mask;       /* cap - 1                          */
  volatile uint32_t head;   /* producer index (write)       */
  volatile uint32_t tail;   /* consumer index (read)        */
  volatile uint32_t max_used;   /* watermark, for sizing     */
  volatile uint32_t dropped;    /* bytes lost on overflow    */
} ringbuf_t;

void     rb_init(ringbuf_t *rb, uint8_t *buf, uint32_t cap);
uint32_t rb_capacity(ringbuf_t *rb);
uint32_t rb_used(ringbuf_t *rb);
uint32_t rb_free(ringbuf_t *rb);

/* Peak occupancy since init. Lets a host observe how close a buffer came to
 * overflowing even after the burst that caused it has drained away. */
uint32_t rb_max_used(ringbuf_t *rb);

/* Push up to len bytes. Returns the number of bytes accepted; the rest is
 * counted in rb->dropped (the caller decides what that means). */
uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t len);

/* Pop up to len bytes into dst. Returns the number of bytes copied. */
uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t len);

/* Zero-copy read: returns a pointer to the first contiguous run and its
 * length. rb_commit_read() advances the tail afterwards.
 * Used to hand UART data straight to tud_cdc_write() without a memcpy. */
const uint8_t *rb_read_ptr(ringbuf_t *rb, uint32_t *len);
void           rb_commit_read(ringbuf_t *rb, uint32_t n);

void     rb_reset(ringbuf_t *rb);

#endif /* RINGBUF_H_ */
