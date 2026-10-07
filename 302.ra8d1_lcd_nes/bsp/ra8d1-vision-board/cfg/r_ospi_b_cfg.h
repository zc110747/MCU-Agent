/* Configuration header for the r_ospi_b driver.
 *
 * The reference project's configuration.xml declares the OSPI pins but never
 * instantiates the OSPI stack, so FSP never emitted an r_ospi_b_cfg.h for this
 * board.  The values below are the ones the FSP generator would emit for a
 * single-line SPI NOR target with no DMAC, no data strobe and no decryption,
 * which is exactly what the board wires up:
 *
 *   OSPI_CS1  = P104      OSPI_SCLK = P808
 *   OSPI_SIO0 = P100      (MOSI)
 *   OSPI_SIO1 = P803      (MISO)
 *   OSPI_SIO2 = P103      (WP)
 *   OSPI_SIO3 = P101      (HOLD/RESET)
 *
 * The device sits on chip-select 1, so bsp_ospi.c opens the driver as
 * OSPI_B_DEVICE_NUMBER_1 and maps XiP at 0x90000000.
 *
 * The driver is taken from FSP v5.1.0, the same release as Drivers/renesas/fsp,
 * so the register names (R_XSPI_*, one xSPI unit on RA8D1) line up with the
 * device header.  A newer driver uses the two-unit RA8M1-style naming
 * (R_XSPI0_*) and does not compile against this device header.
 *
 * No DS (data strobe) pin is routed, so automatic calibration is compiled out.
 */
#ifndef R_OSPI_B_CFG_H_
#define R_OSPI_B_CFG_H_

/* Parameter checking doubles as our argument validator: keep it on. */
#define OSPI_B_CFG_PARAM_CHECKING_ENABLE            (1)

/* No DMAC on this board's OSPI path - the driver falls back to PIO. */
#define OSPI_B_CFG_DMAC_SUPPORT_ENABLE              (0)

/* No data strobe pin -> nothing to calibrate. */
#define OSPI_B_CFG_AUTOCALIBRATION_SUPPORT_ENABLE   (0)

/* XiP IS enabled, and it is the only bulk-read path on this device.
 *
 * The r_ospi_b API has no streaming read: DirectRead() returns
 * FSP_ERR_UNSUPPORTED and DirectTransfer() moves at most the 16 bytes in
 * CDBUF[0].  Every other read route is the AHB bridge map, i.e. XiP.  So the
 * flash is mapped at BSP_OSPI0_CS0_START_ADDRESS (0x80000000) and bulk reads
 * are plain memory loads, while write/erase leave XiP first.
 * See bsp_ospi.c for the enter/exit discipline. */
#define OSPI_B_CFG_XIP_SUPPORT_ENABLE               (1)

/* No on-the-fly decryption. */
#define OSPI_B_CFG_DOTF_SUPPORT_ENABLE              (0)

/* AHB prefetch through the memory map: leave off.  Prefetch across a page
 * boundary is a read-ahead the driver would have to invalidate on every
 * program/erase; the emulator's access pattern (whole 256-byte pages) does not
 * benefit enough to justify the invalidation dance. */
#define OSPI_B_CFG_PREFETCH_FUNCTION                (0x00)

/* Write combining: off for the same reason as prefetch. */
#define OSPI_B_CFG_COMBINATION_FUNCTION             (0x00)

#endif /* R_OSPI_B_CFG_H_ */
