/**
 * @file bsp_ospi.h
 * @brief Board OSPI serial-flash wrapper.
 *
 * The Vision Board fits a Winbond W25Q64-class SPI NOR on the RA8D1 xSPI
 * controller, wired as single-line SPI off chip-select 1:
 *
 *     OSPI_CS1  = P104      OSPI_SCLK = P808
 *     OSPI_SIO0 = P100      (MOSI)
 *     OSPI_SIO1 = P803      (MISO)
 *     OSPI_SIO2 = P103      (WP)
 *     OSPI_SIO3 = P101      (HOLD/RESET)
 *
 * Read those net names as FSP port-pin designators, not as two digits: P104 is
 * BSP_IO_PORT_10_PIN_04 (port 10) while P100 is BSP_IO_PORT_01_PIN_00 (port 1).
 * The two notations are mixed on this board; muxing the wrong port is silent -
 * the write lands on an unrelated pin and the flash answers nothing.
 *
 * Because the part is on CS1, the driver runs with OSPI_B_DEVICE_NUMBER_1 and
 * the XiP window is 0x90000000 (CS0 would be 0x80000000).  See bsp_ospi.c for
 * why the bridge-map access bits have to be re-granted after each XiP
 * transition.
 *
 * Nothing on this board uses the RSIP / DOTF / DMAC paths, so the FSP module
 * is opened with a minimal command set and used purely as a page-addressed
 * NOR: identify, read, page-program, sector-erase.
 *
 * This layer sits between the RT-Thread block device (Phase 3) and the FSP
 * r_ospi_b driver.  It owns no policy - it does not retry, does not cache and
 * does not know about filesystems.
 */
#ifndef BSP_OSPI_H_
#define BSP_OSPI_H_

#include "bsp_api.h"

#include <stdbool.h>
#include <stdint.h>

/** Geometry of the device this board carries.  Overridden by probe results:
 *  bsp_ospi_capacity() reports what the JEDEC ID actually says. */
#define BSP_OSPI_PAGE_SIZE       (256U)
#define BSP_OSPI_SECTOR_SIZE     (4096U)

/** Bring up the xSPI pins, un-gate the controller, open the FSP driver and
 *  read the JEDEC ID.  Safe to call more than once; a second call is a no-op
 *  that returns the first result.
 *
 *  Returns FSP_SUCCESS, or an FSP error code (FSP_ERR_ALREADY_OPEN is never
 *  returned - a repeat call succeeds). */
fsp_err_t bsp_ospi_init (void);

/** True once bsp_ospi_init() has succeeded. */
bool bsp_ospi_ready (void);

/** The three ID bytes last read by bsp_ospi_init(), MSB first
 *  (0xEF 0x40 0x17 for a W25Q64).  Zero before an init that got that far. */
const uint8_t * bsp_ospi_jedec_id (void);

/** True once the ID has been read, even if the part was not recognised.
 *  bsp_ospi_ready() additionally requires a known capacity. */
bool bsp_ospi_probed (void);

/** Device capacity in bytes as decoded from the JEDEC ID, or 0 if unknown. */
uint32_t bsp_ospi_capacity (void);

/** Read `len` bytes starting at `addr` into `dst`.
 *  The flash must be idle; callers serialise through the block device. */
fsp_err_t bsp_ospi_read (uint32_t addr, void * dst, uint32_t len);

/** Page-program `len` bytes at `addr`.  `len` must not cross a page boundary
 *  and the target range must already be erased.  Issues WREN and polls WIP. */
fsp_err_t bsp_ospi_write (uint32_t addr, const void * src, uint32_t len);

/** Erase the 4 KB sector containing `addr`.  Issues WREN and polls WIP. */
fsp_err_t bsp_ospi_erase_sector (uint32_t addr);

/** Last status read (WIP / WEL bits), for the `ospi stat` command. */
uint8_t bsp_ospi_status (void);

/** Bit-bang the JEDEC ID command (0x9F) on the same four SPI pins, bypassing
 *  the xSPI controller entirely.
 *
 *  This exists to separate "the controller is misconfigured" from "there is no
 *  chip on the board".  If the xSPI route returns 00 00 00 but this routine
 *  returns EF 40 17, the wiring is fine and the fault is in the controller
 *  setup; if both return 00 00 00 the device is not answering at all, which
 *  points at the board (unfitted part, missing pull-up on CS, wrong rail).
 *
 *  The four pins are re-muxed to GPIO for the transfer and are left as GPIO
 *  afterwards, so call bsp_ospi_init() again before using the xSPI path.
 *  Results land in bsp_ospi_jedec_id() and bsp_ospi_capacity() so `ospi id`
 *  reports them.
 *
 *  Returns FSP_SUCCESS if the transfer ran to completion (that says nothing
 *  about the bytes read), or an FSP error code.
 */
fsp_err_t bsp_ospi_bitbang_id (void);

/** Copy 12 xSPI/MSTP register values for `ospi regs`.
 *  Layout: WRAPCFG, COMCFG, BMCFGCH0, LIOCFGCS0, BMCTL0, CMCTLCH0, CDCTL0,
 *  CMCFGCS0.CMCFG0/1/2, MSTPCRB, MSTPCRC.  Reads the same instance the driver
 *  wrote, so it is immune to debugger session effects. */
void bsp_ospi_reg_dump (uint32_t out[12]);

/** Read the JEDEC ID by bit-banging the same four signal pins as GPIO.
 *
 *  This bypasses the xSPI controller entirely and so answers the question the
 *  register dump cannot: is there a device on the other end of the wires at
 *  all?  If the controller path returns 00 00 00 but this returns EF 40 17 the
 *  board is fine and the controller setup is at fault; if both are zero the
 *  device is not responding.
 *
 *  Side effect: closes the FSP driver, because an active XiP bridge would keep
 *  driving the pins and fight the GPIO writes.  Call bsp_ospi_init() again
 *  afterwards to return to the controller path.  The ID read lands in the same
 *  buffer bsp_ospi_jedec_id() returns. */
fsp_err_t bsp_ospi_bitbang_id (void);

/** Sample the MISO line (P803) twice: once with no pull-up and once with the
 *  pad's internal pull-up enabled.  This is the decisive measurement when the
 *  flash answers nothing.
 *
 *  *idle_low   - true if MISO sits low with no pull-up (floating or held down)
 *  *float_high - true if MISO reads high once the internal pull-up is on, i.e.
 *                nothing is shorting the line to ground and no device is
 *                driving it low.
 *
 *  Both true means the line is healthy but no device ever drives it - the chip
 *  is absent, unpowered, or its CS never asserts.  *float_high false points at
 *  a short or a device stuck low instead.
 *
 *  Side effect: leaves the OSPI pins as plain GPIO; call bsp_ospi_init() again
 *  to return to the controller path. */
void bsp_ospi_miso_probe (bool * float_high, bool * idle_low);

/** Print the six OSPI pin mux registers (PSEL/PMR) over the console.
 *
 *  The authoritative "is the mux applied?" answer.  Reading the same registers
 *  over SWD is not evidence: a debugger session reports whatever its connect
 *  sequence left in PFS - normally all zeroes, which is indistinguishable from
 *  a mux that was never written.  All six must show PSEL=0x1C, PMR=1. */
void bsp_ospi_pfs_dump (void);

/** Sample the chip-select monitoring path around a burst of JEDEC-ID reads.
 *
 *  Answers a narrower question than it first appears to: *does this instrument
 *  move at all?*  An earlier revision concluded "CS never asserts" from a
 *  reading that was never validated, so this one reports the raw evidence and
 *  leaves the interpretation to the caller.
 *
 *  *idle_high   - COMSTT, read with no transfer in flight
 *  *ever_changed- true if COMSTT differed from its idle value at any point
 *                 during 64 back-to-back 0x9F commands
 *  *idle_sample - the idle COMSTT value
 *  *min_sample  - the lowest COMSTT value seen during the burst
 *
 *  If *ever_changed is false, COMSTT does not track bus activity and cannot
 *  settle the chip-select question either way. */
void bsp_ospi_cs_activity_probe (bool * idle_high, bool * ever_changed,
                                 uint32_t * idle_sample, uint32_t * min_sample);

/** Issue one manual 0x9F and report what the controller observed on the bus.
 *
 *  out[4]: INTS, INTC, COMSTT, BMCTL0 (all uint32_t).
 *
 *  This is the behavioural counterpart to bsp_ospi_reg_dump(): the dump shows
 *  configuration, this shows what the xSPI block actually did.  Bit 0 of INTS
 *  is CMDCMP (the driver spins on it, so it must set or Open() would hang),
 *  bit 8 is the chip-select (ECS) event, bit 20 is an AHB bus error.  CMDCMP
 *  set with no ECS event means the frame finished without asserting CS. */
void bsp_ospi_cmd_int_status (uint32_t out[4]);

#endif /* BSP_OSPI_H_ */
