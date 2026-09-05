#include "ringbuf.h"

void rb_init(ringbuf_t *rb, uint8_t *buf, uint32_t cap) {
  rb->buf      = buf;
  rb->cap      = cap;
  rb->mask     = cap - 1U;
  rb->head     = 0;
  rb->tail     = 0;
  rb->max_used = 0;
  rb->dropped  = 0;
}

void rb_reset(ringbuf_t *rb) {
  uint32_t p = rb_lock();
  rb->head = 0;
  rb->tail = 0;
  rb_unlock(p);
}

uint32_t rb_capacity(ringbuf_t *rb) { return rb->cap; }

uint32_t rb_used(ringbuf_t *rb) {
  uint32_t p = rb_lock();
  uint32_t n = (rb->head - rb->tail) & rb->mask;
  rb_unlock(p);
  return n;
}

/* One byte is deliberately left unused: with head/tail indices alone,
 * head == tail means both "empty" and "exactly full", and confusing the two
 * makes a full buffer look empty (producer overwrites, consumer stalls).
 * Usable capacity is therefore cap-1. */
uint32_t rb_free(ringbuf_t *rb) {
  uint32_t p = rb_lock();
  uint32_t used = (rb->head - rb->tail) & rb->mask;
  uint32_t n = (rb->cap - 1u) - used;
  rb_unlock(p);
  return n;
}

uint32_t rb_max_used(ringbuf_t *rb) {
  uint32_t p = rb_lock();
  uint32_t n = rb->max_used;
  rb_unlock(p);
  return n;
}

uint32_t rb_write(ringbuf_t *rb, const uint8_t *src, uint32_t len) {
  uint32_t p = rb_lock();
  uint32_t used = (rb->head - rb->tail) & rb->mask;
  uint32_t space = (rb->cap - 1u) - used;
  uint32_t n = (len < space) ? len : space;
  uint32_t h = rb->head;
  uint32_t i;

  for (i = 0; i < n; i++) {
    rb->buf[(h + i) & rb->mask] = src[i];
  }
  rb->head = (h + n) & rb->mask;

  if (len > n)                       rb->dropped += (len - n);
  used = (rb->head - rb->tail) & rb->mask;
  if (used > rb->max_used)           rb->max_used = used;

  rb_unlock(p);
  return n;
}

uint32_t rb_read(ringbuf_t *rb, uint8_t *dst, uint32_t len) {
  uint32_t p = rb_lock();
  uint32_t used = (rb->head - rb->tail) & rb->mask;
  uint32_t n = (len < used) ? len : used;
  uint32_t t = rb->tail;
  uint32_t i;

  for (i = 0; i < n; i++) {
    dst[i] = rb->buf[(t + i) & rb->mask];
  }
  rb->tail = (t + n) & rb->mask;

  rb_unlock(p);
  return n;
}

const uint8_t *rb_read_ptr(ringbuf_t *rb, uint32_t *len) {
  uint32_t used = (rb->head - rb->tail) & rb->mask;
  uint32_t t = rb->tail & rb->mask;
  uint32_t run = rb->cap - t;              /* to the end of the array */

  if (used > run) used = run;
  *len = used;
  return &rb->buf[t];
}

void rb_commit_read(ringbuf_t *rb, uint32_t n) {
  uint32_t p = rb_lock();
  rb->tail = (rb->tail + n) & rb->mask;
  rb_unlock(p);
}
