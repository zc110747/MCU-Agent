# SAI clock search for STM32F429 (HSE 25 MHz, PLLM 25 -> VCO input 1 MHz).
#
# Hardware facts taken from the HAL source (Drivers/.../stm32f4xx_hal_sai.c):
#   line 456: MCLK = SAI_CK / (MCKDIV * 2)   and   MCLK = 256 * FS
#   line 465: Mckdiv = (SAI_CK * 10) / (FS * 512) / 10     <- HAL OVERWRITES Init.Mckdiv
#   => FS = SAI_CK / (MCKDIV * 512)      (independent of frame length)
#      BCLK = FS * FrameLength           (32 for 16-bit stereo I2S)
#      MCLK = 256 * FS
#
# SAI_CK (F429) = PLLSAIN * (HSE/PLLM) / (PLLSAIQ * PLLSAIDivQ)
#
# This script brute-forces (N, Q, DivQ, MCKDIV) for every sample rate the
# player can encounter and reports the combination with the smallest error.

VCO_IN = 25_000_000 / 25          # HSE / PLLM  -> 1 MHz

# STM32F429 PLLSAI limits
N_MIN, N_MAX = 50, 432            # PLLSAIN: VCO out must stay in 100..432 MHz
Q_MIN, Q_MAX = 2, 15              # PLLSAIQ
D_MIN, D_MAX = 1, 32              # PLLSAIDivQ
MCK_MIN, MCK_MAX = 1, 15          # MCKDIV[3:0]

RATES = [8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000]


def sai_ck(n, q, d):
    """SAI input clock in Hz."""
    return n * VCO_IN / (q * d)


def vco_out(n):
    return n * VCO_IN


def best_for(rate):
    """Return (err_ppb, n, q, d, mckdiv, fs_actual) with smallest |error|."""
    best = None
    for n in range(N_MIN, N_MAX + 1):
        vco = vco_out(n)
        if not (100e6 <= vco <= 432e6):
            continue
        for q in range(Q_MIN, Q_MAX + 1):
            for d in range(D_MIN, D_MAX + 1):
                ck = sai_ck(n, q, d)
                if ck <= 0:
                    continue
                for mck in range(MCK_MIN, MCK_MAX + 1):
                    fs = ck / (mck * 512)
                    err = abs(fs - rate) / rate
                    # MCLK = 256*FS must stay sane for the WM8978 (<= ~13 MHz)
                    mclk = ck / (mck * 2)
                    if mclk > 26e6:
                        continue
                    key = (err, mck, n, q, d)
                    if best is None or key < best[0]:
                        best = (key, n, q, d, mck, fs)
    key, n, q, d, mck, fs = best
    return key[0], n, q, d, mck, fs


def current_config():
    """What the firmware in the repo actually produces today."""
    n, q, d = 192, 4, 1
    ck = sai_ck(n, q, d)
    rows = []
    for rate in (44100, 48000):
        gcr = int(ck * 10 / (rate * 512))
        mck = gcr // 10
        if (gcr % 10) > 8:
            mck += 1
        fs = ck / (mck * 512)
        rows.append((rate, mck, fs, (fs - rate) / rate * 100.0))
    return ck, rows


def firmware_algorithm():
    """Replicate clock_cfg_for() from bsp/bsp_sai_audio.c EXACTLY.

    The driver only searches the three families and picks the (family, MCKDIV)
    pair minimising |FS - target|, using integer division.  Reproduced here so
    every rate can be checked on the host without having to play a file of
    that rate on the board.
    """
    families = [(271, 2, 6), (172, 7, 1), (213, 13, 1)]
    print("=" * 74)
    print("FIRMWARE ALGORITHM (clock_cfg_for) - all rates, integer maths")
    print("=" * 74)
    print("  %-8s %-6s %-4s %-6s %-8s %-12s %s"
          % ("FS", "N", "Q", "DivQ", "MCKDIV", "actual FS", "error"))
    worst = 0.0
    for rate in RATES:
        best = None
        for (n, q, d) in families:
            ck = int(n * VCO_IN / (q * d))
            for mck in range(1, 16):
                fs = int(ck / (mck * 512))          # integer division, as in C
                err = abs(fs - rate)
                if best is None or err < best[0]:
                    best = (err, n, q, d, mck, ck, fs)
        err, n, q, d, mck, ck, fs = best
        rel = abs(fs - rate) / rate
        worst = max(worst, rel)
        # cents: 1200*log2(ratio) - the musical size of the error
        import math
        cents = 1200.0 * math.log2(fs / rate)
        print("  %-8d %-6d %-4d %-6d %-8d %-12d %+.4f %%  (%+.2f cents)"
              % (rate, n, q, d, mck, fs, (fs - rate) / rate * 100.0, cents))
    print()
    print("  worst-case error: %.4f %%  (audible threshold is roughly 5 cents)"
          % (worst * 100.0))
    return worst


def main():
    print("=" * 74)
    print("CURRENT CONFIG  PLLSAIN=192 PLLSAIQ=4 DivQ=1  -> SAI_CK = %.3f MHz"
          % (sai_ck(192, 4, 1) / 1e6))
    print("=" * 74)
    print("  (Init.Mckdiv=3 in the source is DEAD CODE - HAL recomputes it)")
    ck, rows = current_config()
    print("  %-8s %-8s %-14s %s" % ("target", "MCKDIV", "actual FS", "error"))
    for rate, mck, fs, err in rows:
        print("  %-8d %-8d %-14.3f %+7.3f %%" % (rate, mck, fs, err))
    print()

    print("=" * 74)
    print("OPTIMAL CONFIGS  (VCO in = %.1f MHz, per-rate PLLSAI)" % (VCO_IN / 1e6))
    print("=" * 74)
    print("  %-8s %-7s %-5s %-6s %-8s %-13s %s" %
          ("FS", "N", "Q", "DivQ", "MCKDIV", "actual FS", "error"))
    for rate in RATES:
        err, n, q, d, mck, fs = best_for(rate)
        ck = sai_ck(n, q, d)
        flag = "OK" if err < 0.0005 else "!!"
        print("  %-8d %-7d %-5d %-6d %-8d %-13.4f %+8.4f %%  %s"
              % (rate, n, q, d, mck, fs, (fs - rate) / rate * 100.0, flag))
        print("           SAI_CK=%.6f MHz  VCO=%.1f MHz  MCLK=%.6f MHz  BCLK=%.6f MHz"
              % (ck / 1e6, vco_out(n) / 1e6, ck / (mck * 2) / 1e6, fs * 32 / 1e6))
    print()
    firmware_algorithm()


if __name__ == "__main__":
    main()
