/**
 * @file rt_key_scan.h
 * @brief Serial "virtual gamepad" for the RA8D1 vision board NES port.
 *
 * The upstream project ships board/ports/nes/port/rt_key_scan.{c,h} as empty
 * stubs - the vision board has no physical buttons wired to the NES core, and
 * the touch panel is used by the GUI, not by the emulator.  Rather than leave
 * dead stubs, this port drives the controller from the console: characters
 * typed into the msh terminal are decoded into NES button presses.
 *
 * Design: polling, not IPC.  The upstream stub declared `rt_event_t key_event`
 * and expected the input path to block on it, but the emulator's input is read
 * once per frame (nes_frame -> update_joypad) on the emulator thread, and the
 * console is a character stream with no natural "event boundary".  A lock-free
 * bitmask that nes_read_joypad() samples is both simpler and cannot deadlock
 * against the shell's own reader thread.
 *
 * Key map (both WASD-style and arrow-friendly):
 *   w / W   up          s / S   down        a / A   left      d / D   right
 *   k / K   A button    j / J   B button    n / N   select    m / M   start
 *   space   release all
 *
 * The bit positions match TP_* below, and are translated to the NES joypad
 * shift register order in nes_cpu.h (A1 B1 SE1 ST1 U1 D1 L1 R1, active high =
 * not pressed).
 */
#ifndef _RT_KEY_SCAN_
#define _RT_KEY_SCAN_

#include <rtthread.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Virtual-pad bits.  Values match the upstream stubs so that any code written
   against them keeps compiling. */
#define TP_UP                   (0x01U)
#define TP_DOWN                 (0x02U)
#define TP_LEFT                 (0x04U)
#define TP_RIGHT                (0x08U)
#define TP_SELECT               (0x10U)
#define TP_START                (0x20U)
#define TP_A                    (0x40U)
#define TP_B                    (0x80U)

/* Attach the serial decoder.  Call once, after the console is up. */
void key_scan_init(void);

/* Current pressed-button mask (TP_*).  Safe to call from any thread; reads are
   a single 32-bit load and the whole mask is published atomically. */
uint32_t key_scan(void);

/* Replace the mask outright (used by the `nes key` msh command and by tests,
   which is how the input path is verified without a human at the keyboard). */
void key_scan_set(uint32_t mask);

/* Number of characters decoded since init - lets a test assert that the
   serial path is actually being exercised, not just that the mask reads back. */
uint32_t key_scan_count(void);

/* Human-readable name of a mask, e.g. "UP|A" (never NULL, "" when idle). */
const char *key_scan_name(uint32_t mask);

/* Arm/disarm gamepad capture.  While armed, the msh `nes` command handler
   forwards every character it reads through key_scan_feed() before dropping it,
   which is how the console doubles as a controller. */
void    key_scan_capture(rt_bool_t on);
rt_bool_t key_scan_capture_enabled(void);

/* Decode one console character.  Returns RT_TRUE when the character was a
   gamepad key (the caller should not treat it as shell input). */
rt_bool_t key_scan_feed(char c);

/* Translate a TP_* mask into the NES shift-register word that nes_read_joypad()
   samples (MSB-first, active high = released, player 2 mirrored from player 1). */
uint32_t key_scan_to_nes_joypad(uint32_t mask);

#ifdef __cplusplus
}
#endif

#endif /* _RT_KEY_SCAN_ */
