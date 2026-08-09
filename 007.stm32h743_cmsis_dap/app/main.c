/*----------------------------------------------------------------------------
 * STM32H743 CMSIS-DAP v1 debug probe
 *
 * Glue between the USB HID transport (TinyUSB) and the ARM CMSIS-DAP command
 * processor (third_party/CMSIS-DAP/DAP.c).
 *
 * Threading model
 * ---------------
 * There is none, and that is deliberate. TinyUSB's interrupt handler only
 * pushes events into a queue; every class callback - including
 * tud_hid_set_report_cb() below - is invoked from tud_task(), i.e. from this
 * file's main loop. So the request queue needs no locking and DAP command
 * execution can take as long as it likes without racing the USB stack.
 *
 * The one thing that genuinely is asynchronous is DAP_TransferAbort: the host
 * sends it precisely because a transfer is stuck. It is handled the moment the
 * report arrives instead of being queued behind the work it is meant to cancel.
 *
 * Flow control
 * ------------
 * One response is in flight at a time; requests are buffered DAP_PACKET_COUNT
 * deep, which is exactly what we advertise through DAP_ID_PACKET_COUNT, so a
 * pipelining host can never overrun the queue.
 *--------------------------------------------------------------------------*/

#include <string.h>

#include "bsp.h"
#include "tusb.h"

#include "DAP_config.h"
#include "DAP.h"

/*--------------------------------------------------------------------
 * Request queue
 *
 * Ring buffer with one spare slot so head == tail unambiguously means empty.
 *------------------------------------------------------------------*/
#define REQ_QUEUE_SLOTS   (DAP_PACKET_COUNT + 1U)

static uint8_t  req_queue[REQ_QUEUE_SLOTS][DAP_PACKET_SIZE];
static uint32_t req_head;      /* next slot to write (producer: USB) */
static uint32_t req_tail;      /* next slot to read  (consumer: main) */

static uint8_t  resp_buf[DAP_PACKET_SIZE];
static bool     resp_pending;  /* resp_buf holds a reply waiting for the IN EP */

static inline uint32_t queue_next(uint32_t idx) {
  return (idx + 1U) % REQ_QUEUE_SLOTS;
}

static inline bool queue_empty(void) { return req_head == req_tail; }
static inline bool queue_full(void)  { return queue_next(req_head) == req_tail; }

/*--------------------------------------------------------------------
 * TinyUSB HID callbacks
 *------------------------------------------------------------------*/

/* A GET_REPORT control request. CMSIS-DAP never uses it - all traffic goes
 * through the interrupt endpoints - but the stack requires the symbol. */
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen) {
  (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
  return 0;
}

/* An OUT report arrived: this is a CMSIS-DAP command packet. */
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize) {
  (void)instance;
  (void)report_id;
  (void)report_type;

  if (bufsize == 0U) {
    return;
  }

  /* Out-of-band: abort whatever transfer is currently running. Queuing this
   * would defeat its entire purpose. */
  if (buffer[0] == ID_DAP_TransferAbort) {
    DAP_TransferAbort = 1U;
    return;
  }

  if (queue_full()) {
    /* Can only happen if the host ignores the packet count we advertise.
     * Dropping is the least-bad option: the host will time out and retry. */
    return;
  }

  uint32_t len = (bufsize < DAP_PACKET_SIZE) ? bufsize : DAP_PACKET_SIZE;
  memcpy(req_queue[req_head], buffer, len);
  if (len < DAP_PACKET_SIZE) {
    memset(req_queue[req_head] + len, 0, DAP_PACKET_SIZE - len);
  }
  req_head = queue_next(req_head);
}

/*--------------------------------------------------------------------
 * DAP service
 *------------------------------------------------------------------*/
static void dap_service(void) {
  /* Finish the previous reply first: only one response may be in flight. */
  if (resp_pending) {
    if (tud_hid_ready() && tud_hid_report(0, resp_buf, DAP_PACKET_SIZE)) {
      resp_pending = false;
    }
    return;
  }

  if (queue_empty()) {
    return;
  }

  /* DAP_ExecuteCommand (rather than DAP_ProcessCommand) so that the atomic
   * command group 0x7F/0x7E works - OpenOCD uses it to batch transfers. */
  (void)DAP_ExecuteCommand(req_queue[req_tail], resp_buf);
  req_tail = queue_next(req_tail);

  if (tud_hid_ready() && tud_hid_report(0, resp_buf, DAP_PACKET_SIZE)) {
    resp_pending = false;
  } else {
    resp_pending = true;   /* retry on the next pass */
  }
}

/*--------------------------------------------------------------------
 * USB device state callbacks
 *------------------------------------------------------------------*/
void tud_mount_cb(void) {
  /* Fresh host: drop any half-finished conversation from the previous one.
   * Light the status LED the instant the device is enumerated, so a bench
   * test needs no PC software - plug the USB in and watch PG7 come on. */
  req_head = req_tail = 0;
  resp_pending = false;
  LED_CONNECTED_OUT(1);
}

void tud_umount_cb(void) {
  req_head = req_tail = 0;
  resp_pending = false;
  /* Release the target so it is not left held in reset by a probe that is no
   * longer being driven. */
  PORT_OFF();
  LED_CONNECTED_OUT(0);
}

void tud_suspend_cb(bool remote_wakeup_en) {
  (void)remote_wakeup_en;
}

void tud_resume_cb(void) {
}

/*--------------------------------------------------------------------
 * main
 *------------------------------------------------------------------*/
int main(void) {
  bsp_init();          /* cache, 400 MHz clock tree, USB pins + HSI48/CRS */
  DAP_Setup();         /* DAP_SETUP(): debug pins high-Z, LED, DWT counter */

  tusb_init();

  for (;;) {
    tud_task();        /* runs the USB stack and, from it, the HID callbacks */
    dap_service();     /* executes queued DAP commands, ships the replies    */
  }
}
