/* ---------------------------------------------------------------------------
 * UART4 bridge implementation - see uart_bridge.h for the design notes.
 * -------------------------------------------------------------------------*/

#include "uart_bridge.h"

#include <string.h>

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_uart_ex.h"
#include "stm32h7xx_hal_gpio_ex.h"

/* ---------------------------------------------------------------------------
 * Hardware mapping
 * -------------------------------------------------------------------------*/
#define UART4_PORT          GPIOA
#define UART4_TX_PIN        GPIO_PIN_0     /* PA0 - AF8  (looped to PA1)   */
#define UART4_RX_PIN        GPIO_PIN_1     /* PA1 - AF8                    */
#define UART4_GPIO_AF       GPIO_AF8_UART4

#define UART4_CTS_PORT      GPIOB
#define UART4_CTS_PIN       GPIO_PIN_0     /* PB0 - AF8, CTS input, active low */
#define UART4_RTS_PORT      GPIOB
#define UART4_RTS_PIN       GPIO_PIN_14    /* PB14 - software RTS output, low = ok */

#define UART4_DMA_RX_IRQn   DMA1_Stream0_IRQn
#define UART4_DMA_TX_IRQn   DMA1_Stream1_IRQn

/* All bridge interrupts share one priority so they can never preempt each
 * other - that keeps the shared state variables free of nesting hazards. */
#define BRIDGE_IRQ_PRIO     5u

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

/* ---------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------*/
static UART_HandleTypeDef huart4;
static DMA_HandleTypeDef  hdma_uart4_rx;
static DMA_HandleTypeDef  hdma_uart4_tx;

/* DMA buffers: AXI SRAM, NOLOAD, 32-byte aligned, 32-byte multiple size. */
__attribute__((section(".dma_buf"), aligned(32)))
static uint8_t s_rx_dma[UART_RX_DMA_SIZE];

__attribute__((section(".dma_buf"), aligned(32)))
static uint8_t s_tx_dma[UART_TX_DMA_SIZE];

/* Ring buffer storage: CPU-only, stays in DTCM (fastest, not DMA reachable). */
static uint8_t s_rx_rb_mem[UART_RX_RB_SIZE];
static uint8_t s_tx_rb_mem[UART_TX_RB_SIZE];

ringbuf_t    g_uart_rx_rb;
ringbuf_t    g_uart_tx_rb;
uart_stats_t g_uart_stats;
uart_cfg_t   g_uart_cfg = { 115200u, 8u, 0u, 0u };

static volatile uint32_t s_rx_pos;        /* next byte to read out of s_rx_dma */
static volatile bool     s_rx_pending;    /* an RX event asked for a drain     */
static volatile bool     s_tx_busy;       /* a DMA TX chunk is in flight       */
static volatile bool     s_tx_pending;    /* TX finished / error               */
static uint32_t          s_rx_last_check; /* last time we looked (ms)          */
static bool              s_rts_asserted;
static bool              s_host_rts_ready = true;  /* host RTS from SET_CONTROL_LINE_STATE */
static bool              s_host_dtr       = false; /* boot = disconnected; DTR arms flow  */
static bool              s_flow_applied   = false; /* current CTSE/RTS armed state         */
static uint8_t           s_rx_data_mask;  /* valid data bits in a RX byte      */

/* ---------------------------------------------------------------------------
 * Cache maintenance helpers (Cortex-M7, D-cache enabled, no MPU carve-outs)
 * -------------------------------------------------------------------------*/
static void dma_buf_clean(void *addr, uint32_t len) {
  uint32_t a     = (uint32_t) addr;
  uint32_t start = a & ~31UL;
  uint32_t end   = (a + len + 31UL) & ~31UL;
  SCB_CleanDCache_by_Addr((uint32_t *) start, end - start);
}

static void dma_buf_invalidate(void *addr, uint32_t len) {
  uint32_t a     = (uint32_t) addr;
  uint32_t start = a & ~31UL;
  uint32_t end   = (a + len + 31UL) & ~31UL;
  SCB_InvalidateDCache_by_Addr((uint32_t *) start, end - start);
}

/* ---------------------------------------------------------------------------
 * Flow control helpers
 * -------------------------------------------------------------------------*/
bool uart_bridge_cts_asserted(void) {
  /* CTS is active low: low = peer ready to receive. TX gating by the USART
   * (CTSE) only happens while flow control is armed (port open); this read is
   * still handy for diagnostics at any time. */
  return HAL_GPIO_ReadPin(UART4_CTS_PORT, UART4_CTS_PIN) == GPIO_PIN_RESET;
}

bool uart_bridge_rts_asserted(void) { return s_rts_asserted; }

static void rts_set(bool asserted) {
  s_rts_asserted = asserted;
  HAL_GPIO_WritePin(UART4_RTS_PORT, UART4_RTS_PIN, asserted ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/* ---------------------------------------------------------------------------
 * GPIO / DMA / UART setup
 * -------------------------------------------------------------------------*/
static void uart_gpio_init(void) {
  GPIO_InitTypeDef g = {0};

  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /* TX / RX - alternate function, no pull (there is an external short) */
  g.Pin       = UART4_TX_PIN | UART4_RX_PIN;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = UART4_GPIO_AF;
  HAL_GPIO_Init(UART4_PORT, &g);

  /* CTS - UART4 hardware flow-control input (AF, sampled by the USART itself
   * because HwFlowCtl enables CTS). PULLDOWN so an undriven / floating CTS reads
   * as "active = ready": a peer that does NOT implement flow control must never
   * have its TX gated. The bridge only throttles when the peer actively drives
   * CTS high to stop us. This is the standard, transparent RTS/CTS behaviour. */
  g.Pin       = UART4_CTS_PIN;
  g.Mode      = GPIO_MODE_AF_PP;
  g.Pull      = GPIO_PULLDOWN;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = UART4_GPIO_AF;
  HAL_GPIO_Init(UART4_CTS_PORT, &g);

  /* RTS - plain push-pull output, driven in SOFTWARE from the host RTS signal
   * AND the RX ring headroom (see uart_flow_service). This must NOT be the
   * alternate function: HwFlowCtl is CTS-only, so the USART never drives RTS,
   * and a GPIO write via BSRR reaches the pin. We use software RTS (not the
   * hardware RTS feature) because we want RTS to track our 16 KB RX ring, not
   * the USART's 1-byte TDR. It is armed only while the port is open; at boot
   * (and on disconnect) the pin is held inactive (high) so we do not falsely
   * invite the peer to send. */
  g.Pin       = UART4_RTS_PIN;
  g.Mode      = GPIO_MODE_OUTPUT_PP;
  g.Pull      = GPIO_NOPULL;
  g.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  g.Alternate = 0u;
  HAL_GPIO_Init(UART4_RTS_PORT, &g);
  /* RTS starts inactive (high): flow control is disarmed at boot and the pin
   * is only driven when the port is open (see uart_bridge_connection_service). */
  HAL_GPIO_WritePin(UART4_RTS_PORT, UART4_RTS_PIN, GPIO_PIN_SET);
}

void HAL_UART_MspInit(UART_HandleTypeDef *huart) {
  if (huart->Instance != UART4) return;

  __HAL_RCC_UART4_CLK_ENABLE();
  __HAL_RCC_DMA1_CLK_ENABLE();

  uart_gpio_init();

  /* ---- RX: circular, never stops ---- */
  hdma_uart4_rx.Instance                 = DMA1_Stream0;
  hdma_uart4_rx.Init.Request             = DMA_REQUEST_UART4_RX;
  hdma_uart4_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_uart4_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_uart4_rx.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_uart4_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_uart4_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  hdma_uart4_rx.Init.Mode                = DMA_CIRCULAR;
  hdma_uart4_rx.Init.Priority            = DMA_PRIORITY_VERY_HIGH;
  hdma_uart4_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  hdma_uart4_rx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  hdma_uart4_rx.Init.MemBurst            = DMA_MBURST_SINGLE;
  hdma_uart4_rx.Init.PeriphBurst         = DMA_PBURST_SINGLE;
  __HAL_LINKDMA(huart, hdmarx, hdma_uart4_rx);
  if (HAL_DMA_Init(&hdma_uart4_rx) != HAL_OK) {
    while (1) { }
  }
  HAL_NVIC_SetPriority(UART4_DMA_RX_IRQn, BRIDGE_IRQ_PRIO, 0);
  HAL_NVIC_EnableIRQ(UART4_DMA_RX_IRQn);

  /* ---- TX: normal mode, restarted per chunk ---- */
  hdma_uart4_tx.Instance                 = DMA1_Stream1;
  hdma_uart4_tx.Init.Request             = DMA_REQUEST_UART4_TX;
  hdma_uart4_tx.Init.Direction           = DMA_MEMORY_TO_PERIPH;
  hdma_uart4_tx.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_uart4_tx.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_uart4_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
  hdma_uart4_tx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
  hdma_uart4_tx.Init.Mode                = DMA_NORMAL;
  hdma_uart4_tx.Init.Priority            = DMA_PRIORITY_HIGH;
  hdma_uart4_tx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
  hdma_uart4_tx.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL;
  hdma_uart4_tx.Init.MemBurst            = DMA_MBURST_SINGLE;
  hdma_uart4_tx.Init.PeriphBurst         = DMA_PBURST_SINGLE;
  __HAL_LINKDMA(huart, hdmatx, hdma_uart4_tx);
  if (HAL_DMA_Init(&hdma_uart4_tx) != HAL_OK) {
    while (1) { }
  }
  HAL_NVIC_SetPriority(UART4_DMA_TX_IRQn, BRIDGE_IRQ_PRIO, 0);
  HAL_NVIC_EnableIRQ(UART4_DMA_TX_IRQn);

  HAL_NVIC_SetPriority(UART4_IRQn, BRIDGE_IRQ_PRIO, 0);
  HAL_NVIC_EnableIRQ(UART4_IRQn);
}

static const uart_cfg_t *cfg_defaults(void) {
  static const uart_cfg_t d = { 115200u, 8u, 0u, 0u };
  return &d;
}

/* Translate the logical configuration into HAL terms and restart the
 * receiver. Both DMA channels are stopped first - the bytes in flight belong
 * to the old format and would be garbage anyway. */
static int uart_hw_apply(const uart_cfg_t *cfg) {
  uint32_t total_bits = (uint32_t) cfg->data_bits + ((cfg->parity != 0u) ? 1u : 0u);
  HAL_StatusTypeDef st;

  uint32_t p = rb_lock();
  HAL_UART_DMAStop(&huart4);
  s_tx_busy = false;
  s_tx_pending = true;

  huart4.Init.BaudRate   = cfg->baud;
  huart4.Init.WordLength = (total_bits == 7u) ? UART_WORDLENGTH_7B
                         : (total_bits == 9u) ? UART_WORDLENGTH_9B
                         :                      UART_WORDLENGTH_8B;
  huart4.Init.StopBits   = (cfg->stop_bits == 1u) ? UART_STOPBITS_1_5
                         : (cfg->stop_bits == 2u) ? UART_STOPBITS_2
                         :                          UART_STOPBITS_1;
  huart4.Init.Parity     = (cfg->parity == 1u) ? UART_PARITY_ODD
                         : (cfg->parity == 2u) ? UART_PARITY_EVEN
                         :                       UART_PARITY_NONE;

  /* Hardware flow control is CONNECTION-GATED: armed only while the USB CDC
   * port is open (host DTR asserted), disarmed on disconnect. When armed, CTSE
   * lets the USART sample CTS (PB0) in hardware and halt TX the instant the
   * peer deasserts it, resuming on its own - peer-controlled and therefore
   * transparent. A peer that does not implement flow control leaves CTS
   * floating; PULLDOWN reads that as "ready", so its TX is never gated. When
   * disarmed (s_host_dtr false) CTSE is cleared and the peer can never gate our
   * TX. RTS stays under software control (below) because we want it to track
   * the 16 KB RX ring, not the USART's 1-byte TDR. Set it explicitly here (not
   * just in uart_bridge_init) so a reconfig can never silently inherit a
   * different HwFlowCtl value. */
  huart4.Init.HwFlowCtl = s_host_dtr ? UART_HWCONTROL_CTS : UART_HWCONTROL_NONE;

  st = HAL_UART_Init(&huart4);
  if (st != HAL_OK) {
    rb_unlock(p);
    return -10;
  }

  /* The programmed word length counts the parity bit, so anything below 8
   * data bits puts the parity bit *inside* the byte that DMA hands us:
   * 7 data + parity is an 8-bit word, i.e. RDR bit 7 is parity, not data.
   * Mask the filler bits off. With 8 data bits (with or without parity the
   * word is 8 or 9 bits) the whole byte is real data and no masking is done. */
  s_rx_data_mask = (cfg->data_bits >= 8u)
                 ? 0xFFu
                 : (uint8_t) ((1u << cfg->data_bits) - 1u);

  /* Re-arm the receiver. The buffer was never written by the CPU, so a plain
   * invalidate is safe (no dirty line can be discarded). */
  s_rx_pos = 0;
  s_rx_pending = false;
  dma_buf_invalidate(s_rx_dma, UART_RX_DMA_SIZE);
  if (HAL_UART_Receive_DMA(&huart4, s_rx_dma, (uint16_t) UART_RX_DMA_SIZE) != HAL_OK) {
    rb_unlock(p);
    return -11;
  }
  __HAL_UART_ENABLE_IT(&huart4, UART_IT_IDLE);
  __HAL_UART_CLEAR_IDLEFLAG(&huart4);
  rb_unlock(p);

  rts_set(s_host_dtr);                     /* RTS armed only while connected */
  return 0;
}

void uart_bridge_init(void) {
  rb_init(&g_uart_rx_rb, s_rx_rb_mem, UART_RX_RB_SIZE);
  rb_init(&g_uart_tx_rb, s_tx_rb_mem, UART_TX_RB_SIZE);
  memset(&g_uart_stats, 0, sizeof(g_uart_stats));

  huart4.Instance                    = UART4;
  huart4.Init.BaudRate               = 115200;
  huart4.Init.WordLength             = UART_WORDLENGTH_8B;
  huart4.Init.StopBits               = UART_STOPBITS_1;
  huart4.Init.Parity                 = UART_PARITY_NONE;
  huart4.Init.Mode                   = UART_MODE_TX_RX;
  /* Flow control starts DISARMED: it is armed in uart_bridge_connection_service
   * only after the host opens the port (DTR asserted). uart_hw_apply() decides
   * the real HwFlowCtl value from s_host_dtr, so this is just the boot default. */
  huart4.Init.HwFlowCtl              = UART_HWCONTROL_NONE;
  huart4.Init.OverSampling           = UART_OVERSAMPLING_16;
  huart4.Init.ClockPrescaler         = UART_PRESCALER_DIV1;
  huart4.Init.OneBitSampling         = UART_ONE_BIT_SAMPLE_DISABLE;
  huart4.Init.StopBits               = UART_STOPBITS_1;
  huart4.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

  g_uart_cfg = *cfg_defaults();

  if (uart_hw_apply(&g_uart_cfg) != 0) {
    while (1) { }                 /* unrecoverable - stop for the debugger */
  }
}

int uart_bridge_apply(const uart_cfg_t *cfg) {
  uart_cfg_t saved = g_uart_cfg;

  if (cfg->parity > 2u)     return -1;      /* mark/space not supported     */
  if (cfg->stop_bits > 2u)  return -2;
  if (cfg->baud < 300u || cfg->baud > 6000000u) return -4;

  uint32_t total_bits = (uint32_t) cfg->data_bits + ((cfg->parity != 0u) ? 1u : 0u);
  if (total_bits < 7u || total_bits > 9u) return -5;   /* 5/6 bits unsupported */

  if (uart_hw_apply(cfg) != 0) {
    g_uart_stats.reconf_fail++;
    (void) uart_hw_apply(&saved);           /* put the old format back      */
    return -6;
  }
  g_uart_cfg = *cfg;
  g_uart_stats.reconf_ok++;
  return 0;
}

void uart_bridge_get_cfg(uart_cfg_t *cfg) { *cfg = g_uart_cfg; }

void uart_bridge_line_state(bool host_rts, bool host_dtr) {
  /* Host RTS is passed straight through to our RTS pin (PB14) - the signal we
   * send the UART peer to say "I can accept more bytes". uart_flow_service ANDs
   * it with the RX-ring headroom so we additionally protect the 16 KB ring.
   * Host DTR has no physical pin on UART4 (no DTR/DSR), so it is observed only. */
  s_host_rts_ready = host_rts;
  s_host_dtr       = host_dtr;
}

bool uart_bridge_dtr_asserted(void) { return s_host_dtr; }

/* ---------------------------------------------------------------------------
 * RX: circular DMA -> ring buffer
 * -------------------------------------------------------------------------*/
void uart_rx_service(void) {
  uint32_t now = HAL_GetTick();
  bool     force = ((uint32_t) (now - s_rx_last_check) >= 2u);

  if (!s_rx_pending && !force) return;
  s_rx_pending = false;
  s_rx_last_check = now;

  /* Where the DMA is writing right now. NDTR counts down from the buffer
   * size and reloads automatically, so this is exact at any instant -
   * regardless of which of HT / TC / IDLE woke us up. */
  uint32_t ndtr = __HAL_DMA_GET_COUNTER(&hdma_uart4_rx);
  uint32_t wr   = (UART_RX_DMA_SIZE - (ndtr & (UART_RX_DMA_SIZE - 1u)))
                & (UART_RX_DMA_SIZE - 1u);
  uint32_t head = s_rx_pos & (UART_RX_DMA_SIZE - 1u);
  uint32_t n    = (wr - head) & (UART_RX_DMA_SIZE - 1u);

  if (n == 0u) return;

  uint32_t moved = 0u;
  uint32_t off   = head;
  uint32_t left  = n;

  while (left != 0u) {                       /* at most two spans (wrap)    */
    uint32_t span = left;
    if (off + span > UART_RX_DMA_SIZE) span = UART_RX_DMA_SIZE - off;

    dma_buf_invalidate(&s_rx_dma[off], span);

    /* Strip the parity / filler bits that sit inside the byte in sub-8-bit
     * data modes. Touching s_rx_dma here is safe: every byte in this span has
     * already been handed to the DMA and consumed, so the DMA will not read
     * it again before overwriting it. */
    if (s_rx_data_mask != 0xFFu) {
      for (uint32_t i = 0u; i < span; i++) s_rx_dma[off + i] &= s_rx_data_mask;
    }

    moved += rb_write(&g_uart_rx_rb, &s_rx_dma[off], span);

    off += span;
    if (off >= UART_RX_DMA_SIZE) off -= UART_RX_DMA_SIZE;
    left -= span;
  }

  s_rx_pos = (head + n) & (UART_RX_DMA_SIZE - 1u);

  g_uart_stats.rx_bytes += moved;
  if (n > moved) g_uart_stats.rx_ring_dropped += (n - moved);
  if (n > g_uart_stats.rx_max_span) g_uart_stats.rx_max_span = n;
  g_uart_stats.rx_drain_calls++;
}

/* ---------------------------------------------------------------------------
 * TX: ring buffer -> DMA (CTS throttling is done by the USART hardware)
 * -------------------------------------------------------------------------*/
void uart_tx_service(void) {
  if (s_tx_busy) return;
  s_tx_pending = false;

  /* TX throttling on CTS is done entirely by the USART hardware (CTSE): when
   * the peer deasserts CTS the shift register simply pauses and resumes on its
   * own. We just keep feeding chunks; no software polling, no lost bytes. */
  uint32_t n = rb_used(&g_uart_tx_rb);
  if (n == 0u) return;
  if (n > UART_TX_DMA_SIZE) n = UART_TX_DMA_SIZE;

  uint32_t got = rb_read(&g_uart_tx_rb, s_tx_dma, n);
  if (got == 0u) return;

  dma_buf_clean(s_tx_dma, got);              /* push the copy out to SRAM   */

  s_tx_busy = true;
  if (HAL_UART_Transmit_DMA(&huart4, s_tx_dma, (uint16_t) got) != HAL_OK) {
    s_tx_busy = false;                       /* should not happen; retry    */
    g_uart_stats.tx_ring_dropped += got;     /* bytes we could not hand off */
    return;
  }
  g_uart_stats.tx_bytes  += got;
  g_uart_stats.tx_chunks += 1u;
}

/* ---------------------------------------------------------------------------
 * RTS output follows the RX ring headroom and the host RTS signal
 * -------------------------------------------------------------------------*/
void uart_flow_service(void) {
  /* RTS is only meaningful while flow control is armed (port open). When
   * disarmed keep the pin inactive (high) so we never falsely invite the peer
   * to send data we cannot forward. */
  if (!s_flow_applied) { rts_set(false); return; }

  uint32_t free_b = rb_free(&g_uart_rx_rb);
  uint32_t cap    = rb_capacity(&g_uart_rx_rb);

  /* RTS output = host says "ready to receive" AND we have RX ring room. We
   * deassert (tell the peer to stop) on either condition; we reassert only
   * once the ring has drained back to a comfortable level (hysteresis, so the
   * line does not chatter). This is standard RTS/CTS behaviour and also
   * protects our 16 KB RX ring from overflow. */
  bool desired;
  if (s_rts_asserted) {
    desired = s_host_rts_ready && (free_b >  cap / 8u);
  } else {
    desired = s_host_rts_ready && (free_b >= cap / 4u);
  }
  if (desired != s_rts_asserted) {
    rts_set(desired);
    if (!desired) g_uart_stats.rts_off++;
  }
}

/* ---------------------------------------------------------------------------
 * Arm / disarm CTS/RTS flow control on host port open / close.
 *
 * Called from the main loop (never from an interrupt). The host DTR signal is
 * the only reliable "port open" indication - Windows usbser.sys does not
 * re-forward RTS after open - so we arm flow control when DTR is asserted and
 * disarm it when DTR is dropped. On disconnect we also revert the UART to its
 * default 115200 / 8N1 / no-parity so the next session always starts clean.
 * -------------------------------------------------------------------------*/
void uart_bridge_connection_service(void) {
  bool want = s_host_dtr;                  /* port open => flow control armed */
  if (want == s_flow_applied) return;      /* no change since last time       */

  if (!want) {
    /* Disconnect: revert to the documented default and drop flow control. */
    g_uart_cfg = *cfg_defaults();
  }
  (void) uart_hw_apply(&g_uart_cfg);       /* re-applies HwFlowCtl + RTS from s_host_dtr */
  s_flow_applied = want;
}

/* ---------------------------------------------------------------------------
 * Ring accessors for the USB side
 * -------------------------------------------------------------------------*/
uint32_t uart_bridge_write(const uint8_t *src, uint32_t len) {
  return rb_write(&g_uart_tx_rb, src, len);
}

uint32_t uart_bridge_read(uint8_t *dst, uint32_t len) {
  return rb_read(&g_uart_rx_rb, dst, len);
}

uint32_t uart_bridge_tx_free(void)  { return rb_free(&g_uart_tx_rb); }
uint32_t uart_bridge_rx_used(void)  { return rb_used(&g_uart_rx_rb); }
bool     uart_bridge_tx_idle(void)  { return !s_tx_busy && (rb_used(&g_uart_tx_rb) == 0u); }

uint32_t uart_bridge_clock_hz(void) { return HAL_RCC_GetPCLK1Freq(); }

/* ---------------------------------------------------------------------------
 * Interrupt handlers
 * -------------------------------------------------------------------------*/
void UART4_IRQHandler(void) {
  UART_HandleTypeDef *hu = &huart4;
  uint32_t isr = hu->Instance->ISR;

  /* IDLE: the line went quiet after a frame -> get those bytes out now,
   * even if only a handful of them are waiting. */
  if (isr & USART_ISR_IDLE) {
    __HAL_UART_CLEAR_IDLEFLAG(hu);
    g_uart_stats.rx_idle++;
    s_rx_pending = true;
  }

  /* Error flags must be cleared or the receiver stops accepting data. */
  if (isr & USART_ISR_ORE) {
    __HAL_UART_CLEAR_FLAG(hu, UART_CLEAR_OREF);
    g_uart_stats.overrun_err++;
    s_rx_pending = true;
  }
  if (isr & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE)) {
    if (isr & USART_ISR_PE) { __HAL_UART_CLEAR_FLAG(hu, UART_CLEAR_PEF); g_uart_stats.parity_err++; }
    if (isr & USART_ISR_FE) { __HAL_UART_CLEAR_FLAG(hu, UART_CLEAR_FEF); g_uart_stats.frame_err++; }
    if (isr & USART_ISR_NE) { __HAL_UART_CLEAR_FLAG(hu, UART_CLEAR_NEF); g_uart_stats.noise_err++; }
    s_rx_pending = true;
  }

  /* HAL finishes the DMA transmit sequence: TC -> gState READY -> callback. */
  HAL_UART_IRQHandler(hu);
}

void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_uart4_rx); }
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_uart4_tx); }

/* ---------------------------------------------------------------------------
 * HAL callbacks
 * -------------------------------------------------------------------------*/
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *hu) {
  if (hu->Instance == UART4) {
    g_uart_stats.rx_ht++;
    s_rx_pending = true;                     /* half the buffer is fresh */
  }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *hu) {
  if (hu->Instance == UART4) {
    g_uart_stats.rx_tc++;
    s_rx_pending = true;                     /* wrapped around            */
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *hu) {
  if (hu->Instance == UART4) {
    s_tx_busy = false;
    s_tx_pending = true;                     /* main loop sends the rest  */
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *hu) {
  if (hu->Instance != UART4) return;

  g_uart_stats.overrun_err++;
  s_tx_busy = false;
  s_tx_pending = true;

  /* Make sure the receiver is still armed after a DMA/UART fault. */
  if (hu->hdmarx != NULL) {
    uint32_t p = rb_lock();
    s_rx_pos = 0;
    (void) HAL_UART_Receive_DMA(hu, s_rx_dma, (uint16_t) UART_RX_DMA_SIZE);
    __HAL_UART_ENABLE_IT(hu, UART_IT_IDLE);
    __HAL_UART_CLEAR_IDLEFLAG(hu);
    s_rx_pending = true;
    rb_unlock(p);
  }
}
