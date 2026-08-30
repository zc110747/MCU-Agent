#!/usr/bin/env python3
"""Verify the STM32F429 SAI/WM8978 clock tree AND real playback over SWD.

Why this exists
---------------
The music player's audio clock and streaming cannot be checked with a logic
probe from the PC side, and the board's UART RX is not always available.
Everything needed to prove the clocking and that audio actually streams is
readable from the debug port:

  * RCC_PLLSAICFGR / RCC_DCKCFGR  -> PLLSAIN, PLLSAIQ, PLLSAIDivQ -> SAI_CK
  * SAI1_Block_A CR1              -> MCKDIV, NODIV
  * SAI1_Block_A FRCR / SLOTR     -> frame length, slot count, enabled slots
  * DMA2_Stream3 CR / NDTR        -> DMA actually streaming
  * the driver's own telemetry    -> intended vs measured sample rate
  * player task telemetry         -> prime/refill happened, board is PLAYING

IMPORTANT (hard-won lesson): the debugger mailbox used to inject console
commands (t1 = load+play track 1) lives at addresses that SHIFT every time
the build layout changes.  The addresses are therefore resolved from the ELF
with arm-none-eabi-nm at run time -- never hard-coded.  A previous version
hard-coded 0x20006448/0x20006444; after a rebuild shifted them to
0x20006458/0x20006454 the injection silently wrote to the wrong memory, so the
command never dispatched and every playback check read 0.  That was a
test-harness bug, not a firmware bug.

Clock maths (from Drivers/.../stm32f4xx_hal_sai.c lines 456/465):

    MCLK = SAI_CK / (MCKDIV * 2)      and    MCLK = 256 * FS
    =>  FS   = SAI_CK / (MCKDIV * 512)
        BCLK = FS * FrameLength

Usage:  python tools/audio/verify_sai_clock.py [target_fs_hz] [--play] [--stop]
Exit code 0 only if every check passes.
"""

import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
OPENOCD_CFG = os.path.join(ROOT, "openocd.cfg")
ELF_PATH = os.path.join(ROOT, "build_dbg", "stm32f429_ui_music.elf")
VCO_IN_HZ = 1_000_000  # HSE 25 MHz / PLLM 25

# --- register map -----------------------------------------------------------
REG = {
    "RCC_CR":        0x40023800,
    "RCC_CFGR":      0x40023808,
    "RCC_PLLSAICFGR": 0x40023888,
    "RCC_DCKCFGR":   0x4002388C,
    "SAI1_A_CR1":    0x40015804,
    "SAI1_A_FRCR":   0x4001580C,
    "SAI1_A_SLOTR":  0x40015810,
    "SAI1_A_SR":     0x40015818,
    "DMA2_S3_CR":    0x40020410,
    "DMA2_S3_NDTR":  0x4002042C,
    "TIM2_CNT":      0x40000024,
}

# --- telemetry symbol NAMES (addresses resolved at run time from the ELF) ---
# Singletons only -- s_state is defined in two object files, so it is pinned
# explicitly below via s_need_prime (its singleton neighbour).
SYM_NAMES = [
    "s_tim2_hz",
    "g_sai_fs_measured_hz",
    "g_sai_fs_target_hz",
    "g_sai_mckdiv",
    "g_sai_bclk_hz",
    "g_sai_mclk_hz",
    "g_sai_saick_hz",
    # player / SAI runtime telemetry (read over SWD to prove streaming)
    "g_ply_prime_ok",
    "g_ply_refill",
    "g_ply_refill_eof",
    "g_ply_advance",
    "g_ply_prime_enter",
    "g_player_task_handle",
    "g_sai_dma_cbs",
    "g_sai_tx_dma_ret",
    "hsai",          # SAI_HandleTypeDef; State is its first word
    "s_dec",         # dec_ctx_t; opened is inside (not read directly)
    "s_buf",
    "s_empty_half",
    "s_sai_sem",
    "s_need_prime",  # singleton neighbour of s_state (audio_player.c)
    # decoder progress (localise a stuck prime)
    "g_dec_reads",
    "g_dec_produced",
    "g_dec_fread",
    "g_dec_eof",
    # HAL tick (proves the 1 ms time base is alive during a stalled prime;
    # if uwTick is frozen the SDIO poll-timeout / HAL_Delay spin forever)
    "uwTick",
    # debugger mailbox (RESOLVED, never hard-coded)
    "g_dbg_line",
    "g_dbg_pending",
]

# player_state_t values (audio_player.h)
PLAYER_STOPPED = 0
PLAYER_PLAYING = 1
PLAYER_PAUSED = 2
PLAYER_PRIMING = 3

MDW_RE = re.compile(r"^\s*0x([0-9a-fA-F]{8}):\s+([0-9a-fA-F]{8})\s*$")


def resolve_symbols():
    """Resolve the telemetry symbol addresses from the current ELF via nm so
    the checks never go stale when the build layout shifts."""
    nm = (shutil.which("arm-none-eabi-nm")
          or r"E:\support_tools\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin\arm-none-eabi-nm")
    if not os.path.isfile(ELF_PATH):
        print("WARNING: ELF not found at %s, using register checks only" % ELF_PATH)
        return {}
    try:
        out = subprocess.run([nm, ELF_PATH], capture_output=True, text=True,
                              timeout=60).stdout
    except Exception as e:  # noqa: BLE001
        print("WARNING: nm failed (%s), using register checks only" % e)
        return {}
    sym = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in ("B", "b", "D", "d", "R", "r"):
            sym[parts[2]] = int(parts[0], 16)
    return {n: sym[n] for n in SYM_NAMES if n in sym}


def read_words(addrs, extra_sleep=800):
    """Read every address twice (with a delay) so TIM2 / DMA-callback liveness
    is visible (a moving counter means the DMA ISR is firing)."""
    if not os.path.isfile(OPENOCD_CFG):
        print("ERROR: openocd.cfg not found at %s" % OPENOCD_CFG)
        sys.exit(2)

    cmds = ["init"]
    for a in addrs:
        cmds.append("mdw 0x%08X" % a)
    cmds.append("sleep %d" % extra_sleep)
    for a in addrs:          # second pass -> detect counters that are moving
        cmds.append("mdw 0x%08X" % a)
    cmds.append("shutdown")

    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000"]
    for c in cmds:
        argv += ["-c", c]

    log = os.path.join(ROOT, "tools", "audio", "verify_sai_clock.log")
    p = subprocess.run(argv, capture_output=True, text=True, timeout=180,
                       cwd=ROOT)
    out = (p.stdout or "") + (p.stderr or "")
    with open(log, "w", encoding="utf-8") as fh:
        fh.write(out)

    vals = {}
    for line in out.splitlines():
        m = MDW_RE.match(line)
        if m:
            addr = int(m.group(1), 16)
            vals.setdefault(addr, []).append(int(m.group(2), 16))
    return vals, log


def mww(addr, val):
    """32-bit word write via OpenOCD (target keeps running)."""
    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000",
            "-c", "init",
            "-c", "mww 0x%08X 0x%08X" % (addr, val),
            "-c", "shutdown"]
    subprocess.run(argv, capture_output=True, text=True, timeout=60, cwd=ROOT)


def reset_run():
    """Hard-reset the target and let it run so the test starts from a clean
    power-on state (FatFs scan done, player idle).  Avoids re-priming a board
    that may already be mid-playback / mid-prime from a previous run."""
    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000",
            "-c", "init", "-c", "reset run", "-c", "sleep 2500",
            "-c", "shutdown"]
    r = subprocess.run(argv, capture_output=True, text=True, timeout=60, cwd=ROOT)
    if r.returncode != 0:
        print("WARNING: reset_run returned %d" % r.returncode)


def inject_line(line, dbg_line_addr, dbg_pend_addr):
    """Write a NUL-terminated command line into the debugger mailbox and trigger
    it.  The line is written as 32-bit words (mww) so the whole command lands
    atomically -- per-byte writes raced with the running task, which could
    dispatch a half-written buffer.  g_dbg_pending is set LAST so the running
    task never sees the trigger before the line is fully written."""
    b = line.encode("ascii") + b"\x00"
    while len(b) % 4 != 0:
        b += b"\x00"
    argv = ["openocd", "-f", OPENOCD_CFG,
            "-c", "reset_config none", "-c", "adapter speed 1000", "-c", "init"]
    for i in range(0, len(b), 4):
        word = (b[i]) | (b[i + 1] << 8) | (b[i + 2] << 16) | (b[i + 3] << 24)
        argv += ["-c", "mww 0x%08X 0x%08X" % (dbg_line_addr + i, word)]
    argv += ["-c", "mww 0x%08X 1" % dbg_pend_addr]   # trigger LAST
    argv += ["-c", "sleep 200"]                      # let the task dispatch
    argv += ["-c", "shutdown"]
    r = subprocess.run(argv, capture_output=True, text=True, timeout=60, cwd=ROOT)
    if r.returncode != 0:
        print("WARNING: inject_line '%s' returned %d" % (line, r.returncode))


def main():
    args = sys.argv[1:]
    target = None
    do_play = False
    do_stop = False
    do_reset = False
    play_sleep = 3500          # ms of playback before reading
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--play":
            do_play = True
        elif a == "--stop":
            do_stop = True
        elif a == "--reset":
            do_reset = True
        elif a == "--sleep":
            i += 1
            play_sleep = int(args[i])
        elif a.startswith("--"):
            print("unknown option: %s" % a)
            sys.exit(2)
        else:
            target = int(a)
        i += 1

    sym = resolve_symbols()

    # Resolve the debugger mailbox from the ELF (NEVER hard-code -- it shifts).
    dbg_line_addr = sym.get("g_dbg_line")
    dbg_pend_addr = sym.get("g_dbg_pending")
    if not dbg_line_addr or not dbg_pend_addr:
        print("ERROR: could not resolve g_dbg_line/g_dbg_pending from ELF; "
              "mailbox injection disabled.")
        print("       resolved symbols: %s" % sorted(sym.keys()))
        sys.exit(2)
    print(">> debugger mailbox: g_dbg_line=0x%08X g_dbg_pending=0x%08X"
          % (dbg_line_addr, dbg_pend_addr))

    # Build the read order: registers + resolved telemetry.
    order = list(REG.values()) + [a for a in sym.values()
                                  if a not in REG.values()]
    # If we can resolve s_need_prime, also read the adjacent word that holds
    # s_state (audio_player.c) so we can reconstruct the player state.
    np = sym.get("s_need_prime")
    state_words = []
    if np is not None:
        # s_need_prime (uint8) at np; s_state (int) at np+1 spans np..np+3.
        state_words = [np, np + 4]
        for a in state_words:
            if a not in order:
                order.append(a)

    if do_play:
        if do_reset:
            print(">> hard reset + run (clean state)")
            reset_run()
        print(">> injecting 't1' (load+play track 1) via SWD mailbox")
        inject_line("t1", dbg_line_addr, dbg_pend_addr)
        sys.stdout.flush()

    # Read in one OpenOCD session.  When playback was requested the long
    # "sleep" keeps the target running so the DMA fills the FS-measurement
    # window (32768 frames ~= 0.74 s @ 44.1k) and g_sai_fs_measured_hz settles.
    vals, log = read_words(order, extra_sleep=(play_sleep if do_play else 800))

    def w(name):
        v = vals.get(REG[name])
        return v[0] if v else None

    def wl(name):
        addr = REG.get(name)
        if addr is None:
            addr = sym.get(name)
        v = vals.get(addr) if addr is not None else None
        return v if v else []

    def s(name):
        return vals.get(sym.get(name), [None])[0] if name in sym else None

    need = list(REG) + list(sym)
    missing = [n for n in need if not vals.get(REG.get(n, sym.get(n)))]
    if missing:
        print("ERROR: could not read: %s   (raw log: %s)" % (missing, log))
        sys.exit(2)

    # --- decode clock registers --------------------------------------------
    cr = w("RCC_CR")
    pllsaicfgr = w("RCC_PLLSAICFGR")
    dckcfgr = w("RCC_DCKCFGR")
    cr1 = w("SAI1_A_CR1")
    frcr = w("SAI1_A_FRCR")
    slotr = w("SAI1_A_SLOTR")
    dma_cr = w("DMA2_S3_CR")
    dma_ndtr = w("DMA2_S3_NDTR")
    sai_sr = w("SAI1_A_SR")

    pll_ready = (cr >> 29) & 1
    n = (pllsaicfgr >> 6) & 0x1FF
    q = (pllsaicfgr >> 24) & 0x0F
    divq = ((dckcfgr >> 8) & 0x1F) + 1

    mode = cr1 & 0x03
    prtcfg = (cr1 >> 2) & 0x03
    nodiv = (cr1 >> 19) & 1
    mckdiv = (cr1 >> 20) & 0x0F
    mode_name = {0: "master TX", 1: "master RX",
                 2: "slave TX", 3: "slave RX"}.get(mode, "?")
    prtcfg_name = {0: "free", 1: "SPDIF", 2: "AC97"}.get(prtcfg, "?")

    frl = (frcr & 0xFF) + 1
    fsall = ((frcr >> 8) & 0x7F) + 1
    nbslot = ((slotr >> 8) & 0x0F) + 1
    sloten = (slotr >> 16) & 0xFFFF

    saick = (n * VCO_IN_HZ) / (q * divq) if (q and divq) else 0.0
    mclk = saick / (mckdiv * 2) if mckdiv else 0.0
    fs = saick / (mckdiv * 512) if mckdiv else 0.0
    bclk = fs * frl

    tim2_samples = wl("TIM2_CNT")
    tim2_running = len(tim2_samples) >= 2 and tim2_samples[0] != tim2_samples[1]

    measured = s("g_sai_fs_measured_hz")
    want = target if target else s("g_sai_fs_target_hz")

    # --- decode player state (reconstructed from two adjacent words) -------
    ply_state = None
    ply_need_prime = None
    if np is not None and vals.get(np) and vals.get(np + 4):
        w0 = vals[np][0]
        w1 = vals[np + 4][0]
        ply_need_prime = w0 & 0xFF
        ply_state = (w0 >> 8) | (w1 << 24)
    ply_state_name = {PLAYER_STOPPED: "STOPPED", PLAYER_PLAYING: "PLAYING",
                      PLAYER_PAUSED: "PAUSED", PLAYER_PRIMING: "PRIMING"}.get(
                          ply_state, "??(%s)" % ply_state)

    hsai_state = s("hsai")           # SAI_HandleTypeDef.State is word 0
    dma_en = (dma_cr & 0x01) if dma_cr is not None else None
    # DMA_SxCR CIRC is bit 8 (NOT bit 1).  Bit 1 is DMEIE.
    dma_circ = (dma_cr >> 8) & 0x01 if dma_cr is not None else None
    # SAI_xSR_FLVL is bits 16..18 (0x00070000).  0 = empty.
    fifo_lvl = ((sai_sr if sai_sr is not None else 0) >> 16) & 0x07
    saien = (cr1 & 0x00010000) != 0   # SAI_xCR1_SAIEN is bit 16 (NOT bit 1)
    dmaen = (cr1 & 0x00020000) != 0   # SAI_xCR1_DMAEN is bit 17

    print("=" * 68)
    print("SAI / WM8978 CLOCK + PLAYBACK VERIFICATION (SWD)")
    print("=" * 68)
    print("RCC_CR          = 0x%08X   PLLSAIRDY = %d" % (cr, pll_ready))
    print("RCC_PLLSAICFGR  = 0x%08X   N=%d Q=%d" % (pllsaicfgr, n, q))
    print("RCC_DCKCFGR     = 0x%08X   PLLSAIDivQ=%d" % (dckcfgr, divq))
    print("SAI1_A CR1      = 0x%08X   MODE=%d(%s) PRTCFG=%d(%s) NODIV=%d MCKDIV=%d"
          % (cr1, mode, mode_name, prtcfg, prtcfg_name, nodiv, mckdiv))
    print("SAI1_A FRCR     = 0x%08X   FRL=%d FSALL=%d" % (frcr, frl, fsall))
    print("SAI1_A SLOTR    = 0x%08X   NBSLOT=%d SLOTEN=0x%04X"
          % (slotr, nbslot, sloten))
    print("DMA2_Stream3 CR = 0x%08X   EN=%s CIRC=%s" % (dma_cr, dma_en, dma_circ))
    print("DMA2_Stream3 NDTR= 0x%08X  (2nd read 0x%08X)  moving=%s"
          % (dma_ndtr,
             (vals.get(REG["DMA2_S3_NDTR"])[1]
              if len(vals.get(REG["DMA2_S3_NDTR"], [])) > 1 else dma_ndtr),
             (len(vals.get(REG["DMA2_S3_NDTR"], [])) > 1 and
              vals.get(REG["DMA2_S3_NDTR"])[0] != vals.get(REG["DMA2_S3_NDTR"])[1])))
    print("SAI1_A SR       = 0x%08X   FIFO level=%d (0=empty)" % (sai_sr, fifo_lvl))
    print("SAI CR1 SAIEN(b16)=%s  DMAEN(b17)=%s" % (saien, dmaen))
    print("-" * 68)
    print("SAI_CK  = %12.3f Hz" % saick)
    print("MCLK    = %12.3f Hz   (PE2 -> WM8978 MCLK, = 256*FS)" % mclk)
    print("BCLK    = %12.3f Hz   (PE5, = FS * %d)" % (bclk, frl))
    print("FS      = %12.3f Hz   (LRCK, from registers)" % fs)
    if want:
        print("target  = %12d Hz   error %+.4f %%" % (want, (fs - want) / want * 100.0))
    print("-" * 68)
    print("driver telemetry: SAI_CK=%d  MCLK=%d  BCLK=%d  MCKDIV=%d  target=%d"
          % (s("g_sai_saick_hz"), s("g_sai_mclk_hz"), s("g_sai_bclk_hz"),
             s("g_sai_mckdiv"), s("g_sai_fs_target_hz")))
    print("player state     = %s  (need_prime=%s)"
          % (ply_state_name, ply_need_prime))
    print("hsai.State       = %d  (1=READY, 2=BUSY)" % hsai_state)
    print("g_ply_prime_ok   = %d" % (s("g_ply_prime_ok") or 0))
    print("g_ply_refill     = %d" % (s("g_ply_refill") or 0))
    print("g_ply_refill_eof = %d" % (s("g_ply_refill_eof") or 0))
    print("g_ply_advance    = %d" % (s("g_ply_advance") or 0))
    print("g_ply_prime_enter= %d  (>=1 => prime started; ==0 => never entered)"
          % (s("g_ply_prime_enter") or 0))
    print("g_player_task_handle = 0x%08X" % (s("g_player_task_handle") or 0))
    print("g_sai_dma_cbs    = %s  (moving => DMA ISR firing)"
          % wl("g_sai_dma_cbs"))
    txret = s("g_sai_tx_dma_ret")
    txret_name = {0: "HAL_OK", 1: "HAL_BUSY", 2: "HAL_ERROR",
                  3: "HAL_TIMEOUT", 0xFFFFFFFF: "never called"}.get(
                      txret, "??(%s)" % txret)
    print("g_sai_tx_dma_ret = %s  (%s)" % (txret, txret_name))
    print("decoder: reads=%d produced=%d fread=%d eof=%d"
          % (s("g_dec_reads") or 0, s("g_dec_produced") or 0,
             s("g_dec_fread") or 0, s("g_dec_eof") or 0))
    print("TIM2 (FS time base) running: %s   samples=%s"
          % ("yes" if tim2_running else "NO",
             [hex(x) for x in tim2_samples[:2]]))
    uw_t = wl("uwTick")
    uw_moving = len(uw_t) >= 2 and uw_t[0] != uw_t[1]
    print("HAL tick uwTick: %s  (moving => 1 ms time base alive, SDIO timeout works)"
          % ("alive (%s -> %s)" % (uw_t[0], uw_t[1])
             if uw_moving else "FROZEN %s" % (uw_t[0] if uw_t else None)))
    print("measured FS (needs playback): %d Hz" % measured)
    print("=" * 68)

    if do_stop:
        print(">> injecting 'x' (stop playback) via SWD mailbox")
        inject_line("x", dbg_line_addr, dbg_pend_addr)

    # --- PASS / FAIL --------------------------------------------------------
    checks = []
    checks.append(("PLLSAI locked (PLLSAIRDY=1)", pll_ready == 1))
    checks.append(("MCLK divider active (NODIV=0)", nodiv == 0))
    checks.append(("frame length 32 bits (I2S 16-bit stereo)", frl == 32))
    checks.append(("2 slots declared", nbslot == 2))
    checks.append(("slots enabled (SLOTEN!=0)", sloten != 0))
    checks.append(("MCKDIV non-zero", mckdiv != 0))
    checks.append(("SAI master TX (MODE=0)", mode == 0))
    checks.append(("free protocol (PRTCFG=0)", prtcfg == 0))
    checks.append(("MCLK == 256*FS", abs(mclk - 256 * fs) < 1.0))
    if mckdiv:
        checks.append(("telemetry MCKDIV matches CR1", s("g_sai_mckdiv") == mckdiv))
        checks.append(("telemetry SAI_CK matches registers",
                       abs(s("g_sai_saick_hz") - saick) < 2.0))
    if want:
        err = abs(fs - want) / want
        checks.append(("FS within 0.1%% of %d Hz (err %.4f%%)" % (want, err * 100),
                       err < 0.001))

    # Playback-specific checks (only meaningful when --play was given).
    if do_play:
        checks.append(("command dispatched (need_prime consumed = 0)",
                       ply_need_prime == 0))
        checks.append(("prime actually entered (g_ply_prime_enter>=1)",
                       (s("g_ply_prime_enter") or 0) >= 1))
        checks.append(("decoder produced PCM (g_dec_produced>0)",
                       (s("g_dec_produced") or 0) > 0))
        checks.append(("player reached PLAYING (not stuck PRIMING/STOPPED)",
                       ply_state == PLAYER_PLAYING))
        checks.append(("prime decoded real data (g_ply_prime_ok>=1)",
                       (s("g_ply_prime_ok") or 0) >= 1))
        checks.append(("SAI peripheral enabled (CR1 SAIEN=1)",
                       saien))
        checks.append(("SAI DMA request enabled (CR1 DMAEN=1)",
                       dmaen))
        checks.append(("HAL_SAI_Transmit_DMA returned HAL_OK",
                       txret == 0))
        ndtr_v = vals.get(REG["DMA2_S3_NDTR"], [])
        ndtr_moving = len(ndtr_v) > 1 and ndtr_v[0] != ndtr_v[1]
        checks.append(("DMA actually transferring (NDTR changing)",
                       ndtr_moving))
        checks.append(("DMA stream enabled (EN=1)",
                       dma_en == 1))
        moving = (len(wl("g_sai_dma_cbs")) >= 2
                  and wl("g_sai_dma_cbs")[0] != wl("g_sai_dma_cbs")[-1])
        checks.append(("DMA ISR firing (g_sai_dma_cbs advancing)",
                       moving))
        if measured:
            merr = abs(measured - fs) / fs
            checks.append(("measured FS within 0.5%% of computed (err %.4f%%)"
                           % (merr * 100), merr < 0.005))
        else:
            print("NOTE: g_sai_fs_measured_hz is 0 -> playback did not stream.")
            print("      This means the prime never completed or SAI/DMA did not")

    print()
    npass = 0
    for name, ok in checks:
        print("  [%s] %s" % ("PASS" if ok else "FAIL", name))
        npass += 1 if ok else 0
    nfail = len(checks) - npass
    print()
    print("RESULT: %d passed, %d failed" % (npass, nfail))
    print("raw log: %s" % log)
    sys.exit(0 if nfail == 0 else 1)


if __name__ == "__main__":
    main()
