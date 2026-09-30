/**
 ******************************************************************************
 * @file    sys_prob.c
 * @brief   Camera / DVP diagnostic probes - pin / pull / HREF bus probe, raw
 *          bus trace, register dump, register snapshot and SCCB poke.
 *
 * Everything here is debug-only and is compiled out entirely unless
 * CAM_DIAGNOSTICS is defined (see bsp/sys_prob.h and the CMake option
 * ENABLE_CAM_DIAGNOSTICS). The probes are SWD-triggered: write the matching
 * <group>.req field and wait for <group>.done. They borrow the live capture
 * pipeline through the public bsp_camera_* hooks, so they never touch the
 * production data path directly.
 ******************************************************************************
 */

#include "sys_prob.h"

#ifdef CAM_DIAGNOSTICS

/* ---- Sensor register visibility (debug) ---------------------------------
 * Read back after OV5640_Start() and parked in RAM so the DVP register map can
 * be inspected over SWD. The pad-output-enable pair is the interesting one:
 *   0x3017 bit6 VSYNC, bit5 HREF, bit4 PCLK, bit[3:0] D[9:6]
 *   0x3018 bit[7:2] D[5:0]
 * Sync pads enabled while the data pads stay tri-stated gives correct frame
 * timing over a floating data bus - i.e. one drifting value per line. */
static const uint16_t cam_reg_addr[CAM_REG_SNAP_N] = {
    0x3008,
    0x300E,
    0x3017,
    0x3018,
    0x3034,
    0x3035,
    0x3036,
    0x3037,
    0x3108,
    0x3821,
    0x4300,
    0x501F,
    0x4740,
    0x3808,
    0x3809,
    0x380A,
    0x380B,
    0x503D,
};

/* Sample the eleven DVP signals as a single 11-bit word.
 * Bit order in all three masks:
 *   0..7 = D0..D7, 8 = HSYNC, 9 = VSYNC, 10 = PIXCLK */
static inline uint32_t dvp_sample(void)
{
    const uint32_t a = GPIOA->IDR, c = GPIOC->IDR, d = GPIOD->IDR;
    const uint32_t e = GPIOE->IDR, g = GPIOG->IDR;

    return (((c >> 6) & 1U) << 0) |  /* D0    PC6  */
           (((c >> 7) & 1U) << 1) |  /* D1    PC7  */
           (((g >> 10) & 1U) << 2) | /* D2    PG10 */
           (((g >> 11) & 1U) << 3) | /* D3    PG11 */
           (((e >> 4) & 1U) << 4) |  /* D4    PE4  */
           (((d >> 3) & 1U) << 5) |  /* D5    PD3  */
           (((e >> 5) & 1U) << 6) |  /* D6    PE5  */
           (((e >> 6) & 1U) << 7) |  /* D7    PE6  */
           (((a >> 4) & 1U) << 8) |  /* HSYNC PA4  */
           (((g >> 9) & 1U) << 9) |  /* VSYNC PG9  */
           (((a >> 6) & 1U) << 10);  /* PCLK  PA6  */
}

static sys_prob_state_t s_sys_prob = {
    .regdump.regdump_base = 0x3800U,
};

sys_prob_state_t *sys_prob_get_state(void)
{
    return &s_sys_prob;
}

/* ---- Physical-layer pin probe -------------------------------------------
 * Write pin.probe_req = 1 from the debugger to detach the 11 DVP signals from
 * the DCMI, sample them as raw GPIO inputs for a while, and report which ones
 * actually move. pin_edges counts transitions so a slow line can be told from
 * a fast one. A line that never reaches 1 (or never reaches 0) is stuck -
 * wiring, pad enable or alternate-function problem - and no amount of DCMI
 * tuning fixes it. */
void sys_prob_probe_pins(void)
{
    const bool was_running = bsp_camera_is_auto_running();

    if (was_running)
    {
        bsp_camera_stop();
    }
    bsp_camera_dvp_pins_mode(true);

    uint32_t ones = 0, zeros = 0;
    uint32_t edges[11] = {0};
    uint32_t prev      = dvp_sample();

    /* ~400 k samples: at 480 MHz this spans several milliseconds, i.e. many
     * whole lines, so even the slowest signal (VSYNC) gets a chance to move. */
    for (uint32_t i = 0; i < 400000U; i++)
    {
        const uint32_t v = dvp_sample();
        ones |= v;
        zeros |= ~v;
        const uint32_t ch = v ^ prev;
        if (ch)
        {
            for (uint32_t b = 0; b < 11U; b++)
            {
                if (ch & (1U << b))
                {
                    edges[b]++;
                }
            }
            prev = v;
        }
    }

    s_sys_prob.pin.pin_ones  = ones & 0x7FFU;
    s_sys_prob.pin.pin_zeros = zeros & 0x7FFU;
    for (uint32_t b = 0; b < 11U; b++)
    {
        s_sys_prob.pin.pin_edges[b] = edges[b];
    }

    bsp_camera_dvp_pins_mode(false);
    if (was_running)
    {
        bsp_camera_restart_continuous();
    }
    s_sys_prob.pin.probe_done++;
}

/* Re-init the eleven DVP lines as inputs with the requested pull setting, then
 * report the AND/OR of a long sample run so floating vs. driven lines can be
 * told apart. */
void sys_prob_probe_pull(void)
{
    static const uint32_t pulls[3] = {
        GPIO_NOPULL,
        GPIO_PULLUP,
        GPIO_PULLDOWN,
    };
    const bool was_running = bsp_camera_is_auto_running();

    if (was_running)
    {
        bsp_camera_stop();
    }

    for (uint32_t k = 0; k < 3U; k++)
    {
        bsp_camera_dvp_pins_pull(pulls[k]);
        HAL_Delay(2); /* let the ~40 kOhm pull settle against the pin capacitance */

        uint32_t acc_and = 0x7FFU;
        uint32_t acc_or  = 0U;
        for (uint32_t i = 0; i < 200000U; i++)
        {
            const uint32_t v = dvp_sample();
            acc_and &= v;
            acc_or |= v;
        }
        s_sys_prob.pull.pull_and[k] = acc_and & 0x7FFU;
        s_sys_prob.pull.pull_or[k]  = acc_or & 0x7FFU;
    }

    bsp_camera_dvp_pins_mode(false);
    if (was_running)
    {
        bsp_camera_restart_continuous();
    }
    s_sys_prob.pull.pull_done++;
}

/* Sample D[7:0] for the duration of a single HREF-active window.
 *
 * The free-running pin probe cannot tell "bus is busy during blanking" from
 * "bus carries pixels", because it averages over both. This one locks onto a
 * line and answers the only question left: while the DCMI considers the data
 * valid, does the bus actually change? */
void sys_prob_probe_href(void)
{
    const bool was_running = bsp_camera_is_auto_running();

    if (was_running)
    {
        bsp_camera_stop();
    }
    bsp_camera_dvp_pins_mode(true);

    static uint8_t seen_hi[256];
    static uint8_t seen_lo[256];
    for (uint32_t i = 0; i < 256U; i++)
    {
        seen_hi[i] = 0;
        seen_lo[i] = 0;
    }

    uint32_t hi_count = 0, lo_count = 0;
    uint32_t hi_distinct = 0, lo_distinct = 0;
    uint32_t href_edges = 0, vsync_edges = 0;
    uint32_t prev_href = 0, prev_vsync = 0;
    uint32_t hi_written = 0, lo_written = 0;

    /* ~600 k samples spans several whole frames at any plausible PCLK rate. */
    for (uint32_t i = 0; i < 600000U; i++)
    {
        const uint32_t a  = GPIOA->IDR;
        const uint32_t c  = GPIOC->IDR;
        const uint32_t dd = GPIOD->IDR;
        const uint32_t e  = GPIOE->IDR;
        const uint32_t g  = GPIOG->IDR;

        const uint8_t href  = (uint8_t)((a >> 4) & 1U); /* PA4  */
        const uint8_t vsync = (uint8_t)((g >> 9) & 1U); /* PG9  */
        const uint8_t v     = (uint8_t)((((c >> 6) & 1U) << 0) |
                                        (((c >> 7) & 1U) << 1) |
                                        (((g >> 10) & 1U) << 2) |
                                        (((g >> 11) & 1U) << 3) |
                                        (((e >> 4) & 1U) << 4) |
                                        (((dd >> 3) & 1U) << 5) |
                                        (((e >> 5) & 1U) << 6) |
                                        (((e >> 6) & 1U) << 7));

        if (href != 0U)
        {
            hi_count++;
            if (seen_hi[v] == 0U)
            {
                seen_hi[v] = 1U;
                hi_distinct++;
            }
            if (hi_written < 16U)
            {
                s_sys_prob.bus.bus_hi_samples[hi_written++] = v;
            }
        }
        else
        {
            lo_count++;
            if (seen_lo[v] == 0U)
            {
                seen_lo[v] = 1U;
                lo_distinct++;
            }
            if (lo_written < 16U)
            {
                s_sys_prob.bus.bus_lo_samples[lo_written++] = v;
            }
        }

        if (href != prev_href)
        {
            href_edges++;
            prev_href = href;
        }
        if (vsync != prev_vsync)
        {
            vsync_edges++;
            prev_vsync = vsync;
        }
    }

    s_sys_prob.bus.bus_hi_count    = hi_count;
    s_sys_prob.bus.bus_lo_count    = lo_count;
    s_sys_prob.bus.bus_hi_distinct = hi_distinct;
    s_sys_prob.bus.bus_lo_distinct = lo_distinct;
    s_sys_prob.pin.href_edges      = href_edges;
    s_sys_prob.pin.vsync_edges     = vsync_edges;

    bsp_camera_dvp_pins_mode(false);
    if (was_running)
    {
        bsp_camera_restart_continuous();
    }
    s_sys_prob.pin.probe_done++;
}

/* Free-run the DVP bus into the trace buffer at maximum load bandwidth. See the
 * comment next to sys_prob_state_t.bus.trace for the slot layout and what the
 * result proves. */
void sys_prob_trace_bus(void)
{
    const bool was_running = bsp_camera_is_auto_running();

    if (was_running)
    {
        bsp_camera_stop();
    }
    bsp_camera_dvp_pins_mode(true);

    volatile const uint32_t *const pa = &GPIOA->IDR;
    volatile const uint32_t *const pe = &GPIOE->IDR;
    uint32_t                      *p  = s_sys_prob.bus.trace;

    __disable_irq();

    /* Start the window on a line boundary: wait for HSYNC to be low, then for
     * the rising edge. Bounded so a dead sync line cannot hang the main loop. */
    for (uint32_t g = 4000000U; g != 0U; g--)
    {
        if ((((*pa) >> 4) & 1U) == 0U)
        {
            break;
        }
    }
    for (uint32_t g = 4000000U; g != 0U; g--)
    {
        if ((((*pa) >> 4) & 1U) != 0U)
        {
            break;
        }
    }

    /* Unrolled by eight so the loop overhead does not eat into the sample rate.
     * Two loads and two stores per pair, no decoding whatsoever. */
    for (uint32_t i = 0; i < CAM_TRACE_N; i += 8U)
    {
        p[0] = *pa;
        p[1] = *pe;
        p[2] = *pa;
        p[3] = *pe;
        p[4] = *pa;
        p[5] = *pe;
        p[6] = *pa;
        p[7] = *pe;
        p += 8;
    }

    __enable_irq();

    bsp_camera_dvp_pins_mode(false);
    if (was_running)
    {
        bsp_camera_restart_continuous();
    }
    s_sys_prob.bus.trace_done++;
}

/* Dump 64 consecutive SCCB registers starting at regdump_base. 0xEE marks a
 * failed read. */
void sys_prob_regdump(void)
{
    const uint16_t base = s_sys_prob.regdump.regdump_base;

    for (uint32_t i = 0; i < CAM_REGDUMP_N; i++)
    {
        uint8_t v = 0;
        if (bsp_camera_read_reg((uint16_t)(base + i), &v) != 0)
        {
            v = 0xEE;
        }
        s_sys_prob.regdump.regdump[i] = v;
    }
    s_sys_prob.regdump.regdump_done++;
}

/* Snapshot the fixed CAM_REG_SNAP_N register window the timing/sync config
 * lives in, so the whole DVP setup can be inspected over SWD. */
void sys_prob_snapshot_regs(void)
{
    for (uint32_t i = 0; i < CAM_REG_SNAP_N; i++)
    {
        uint8_t v = 0;
        if (bsp_camera_read_reg(cam_reg_addr[i], &v) != 0)
        {
            v = 0xEE; /* marker: the I2C read itself failed */
        }
        s_sys_prob.regdump.reg_val[i] = v;
    }
}

/* Drain the pending probe / trace / regdump / poke requests. Mirrors the old
 * req-gated blocks that used to live in bsp_camera_service(); the fields are
 * the same, only the owning struct moved here. */
void sys_prob_service(void)
{
    sys_prob_state_t *st = sys_prob_get_state();

    if (st->pin.probe_req != 0U)
    {
        st->pin.probe_req = 0;
        sys_prob_probe_pins();
    }

    if (st->pin.href_req != 0U)
    {
        st->pin.href_req = 0;
        sys_prob_probe_href();
    }

    if (st->bus.trace_req != 0U)
    {
        st->bus.trace_req = 0;
        sys_prob_trace_bus();
    }

    if (st->pull.pull_req != 0U)
    {
        st->pull.pull_req = 0;
        sys_prob_probe_pull();
    }

    if (st->regdump.regdump_req != 0U)
    {
        st->regdump.regdump_req = 0;
        sys_prob_regdump();
    }

    if (st->poke.poke_req != 0U)
    {
        st->poke.poke_req = 0;
        (void)bsp_camera_write_reg((uint16_t)st->poke.poke_reg, (uint8_t)st->poke.poke_val);
        st->poke.poke_done++;
    }
}

#endif /* CAM_DIAGNOSTICS */
