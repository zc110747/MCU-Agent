/**
 ******************************************************************************
 * @file    sys_prob.h
 * @brief   Camera / DVP diagnostic probe module.
 *
 * Compiled out entirely unless CAM_DIAGNOSTICS is defined (see the CMake
 * option ENABLE_CAM_DIAGNOSTICS). When disabled, none of the probe code, the
 * 16 KB bus-trace buffer, or the SWD-readable diagnostic state is linked into
 * the production binary - so the debug knobs cost zero flash / RAM / runtime
 * in a normal build.
 *
 * All probes are SWD-triggered: write the matching <group>.req field from the
 * debugger and wait for <group>.done. They borrow the live capture pipeline
 * through the public bsp_camera_* hooks and never touch the production data
 * path directly.
 ******************************************************************************
 */

#ifndef SYS_PROB_H
#define SYS_PROB_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bsp_camera.h"

/* Diagnostic buffer / window sizes (debug only). */
#define CAM_REG_SNAP_N 18U
#define CAM_TRACE_N    4096U
#define CAM_REGDUMP_N  64U

#ifdef CAM_DIAGNOSTICS

typedef struct
{
    volatile uint32_t probe_req;
    volatile uint32_t probe_done;
    volatile uint32_t pin_ones;
    volatile uint32_t pin_zeros;
    volatile uint32_t pin_edges[11];
    volatile uint32_t href_req;
    volatile uint32_t href_edges;
    volatile uint32_t vsync_edges;
} sys_prob_pin_t;

typedef struct
{
    volatile uint32_t trace_req;
    volatile uint32_t trace_done;
    uint32_t          trace[CAM_TRACE_N];
    volatile uint8_t  bus_hi_samples[16];
    volatile uint8_t  bus_lo_samples[16];
    volatile uint32_t bus_hi_distinct;
    volatile uint32_t bus_lo_distinct;
    volatile uint32_t bus_hi_count;
    volatile uint32_t bus_lo_count;
} sys_prob_bus_t;

typedef struct
{
    volatile uint32_t pull_req;
    volatile uint32_t pull_done;
    volatile uint32_t pull_and[3];
    volatile uint32_t pull_or[3];
} sys_prob_pull_t;

typedef struct
{
    volatile uint16_t regdump_base;
    volatile uint32_t regdump_req;
    volatile uint32_t regdump_done;
    volatile uint8_t  regdump[CAM_REGDUMP_N];
    volatile uint8_t  reg_val[CAM_REG_SNAP_N];
} sys_prob_regdump_t;

typedef struct
{
    volatile uint32_t poke_req;
    volatile uint32_t poke_reg;
    volatile uint32_t poke_val;
    volatile uint32_t poke_done;
} sys_prob_poke_t;

typedef struct
{
    sys_prob_pin_t     pin;
    sys_prob_bus_t     bus;
    sys_prob_pull_t    pull;
    sys_prob_regdump_t regdump;
    sys_prob_poke_t    poke;
} sys_prob_state_t;

/* Returns the file-static diagnostic state. Reachable over SWD as
 *   sys_prob_get_state()-><group>.<field>
 * Set <group>.req = 1 from the debugger to run the matching probe / trace /
 * regdump / poke, then poll <group>.done. Field names are unchanged from the
 * old s_cam_diag.<group>.* layout, so existing debug scripts keep working. */
sys_prob_state_t *sys_prob_get_state(void);

/* Called from bsp_camera_service() on every super-loop pass while CAM_DIAGNOSTICS
 * is enabled; drains the pending probe / trace / regdump / poke requests. */
void sys_prob_service(void);

void sys_prob_probe_pull(void);
void sys_prob_probe_pins(void);
void sys_prob_probe_href(void);
void sys_prob_trace_bus(void);
void sys_prob_regdump(void);
void sys_prob_snapshot_regs(void);

#endif /* CAM_DIAGNOSTICS */

#ifdef __cplusplus
}
#endif

#endif /* SYS_PROB_H */
