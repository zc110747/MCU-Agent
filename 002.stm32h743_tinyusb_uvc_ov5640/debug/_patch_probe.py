import io

p = r"D:/data/workspace/stm32_tinyusb/Core/Src/bsp_camera.c"
s = open(p, "r", encoding="utf-8").read()

old = """  uint32_t guard = 0;
  while (((GPIOA->IDR >> 4) & 1U) != 0U && ++guard < 20000000U) { }
  guard = 0;
  while (((GPIOA->IDR >> 4) & 1U) == 0U && ++guard < 20000000U) { }

  static uint8_t seen[256];
  for (uint32_t i = 0; i < 256U; i++) {
    seen[i] = 0;
  }

  uint32_t n = 0, distinct = 0;
  while (((GPIOA->IDR >> 4) & 1U) != 0U && n < 200000U) {
    const uint32_t c = GPIOC->IDR, dd = GPIOD->IDR;
    const uint32_t e = GPIOE->IDR, g = GPIOG->IDR;

    const uint8_t v = (uint8_t)((((c >>  6) & 1U) << 0) |
                                (((c >>  7) & 1U) << 1) |
                                (((g >> 10) & 1U) << 2) |
                                (((g >> 11) & 1U) << 3) |
                                (((e >>  4) & 1U) << 4) |
                                (((dd >> 3) & 1U) << 5) |
                                (((e >>  5) & 1U) << 6) |
                                (((e >>  6) & 1U) << 7));

    if (n < 64U) {
      cam_href_samples[n] = v;
    }
    if (seen[v] == 0U) {
      seen[v] = 1U;
      distinct++;
    }
    n++;
  }

  cam_href_count    = n;
  cam_href_distinct = distinct;
"""

new = """  static uint8_t seen_hi[256];
  static uint8_t seen_lo[256];
  for (uint32_t i = 0; i < 256U; i++) {
    seen_hi[i] = 0;
    seen_lo[i] = 0;
  }

  uint32_t hi_count = 0, lo_count = 0;
  uint32_t hi_distinct = 0, lo_distinct = 0;
  uint32_t href_edges = 0, vsync_edges = 0;
  uint32_t prev_href = 0, prev_vsync = 0;
  uint32_t hi_written = 0, lo_written = 0;

  /* ~600 k samples spans several whole frames at any plausible PCLK rate. */
  for (uint32_t i = 0; i < 600000U; i++) {
    const uint32_t a = GPIOA->IDR;
    const uint32_t c = GPIOC->IDR;
    const uint32_t dd = GPIOD->IDR;
    const uint32_t e = GPIOE->IDR;
    const uint32_t g = GPIOG->IDR;

    const uint8_t href  = (uint8_t)((a >> 4) & 1U);   /* PA4  */
    const uint8_t vsync = (uint8_t)((g >> 9) & 1U);   /* PG9  */
    const uint8_t v = (uint8_t)((((c >>  6) & 1U) << 0) |
                                (((c >>  7) & 1U) << 1) |
                                (((g >> 10) & 1U) << 2) |
                                (((g >> 11) & 1U) << 3) |
                                (((e >>  4) & 1U) << 4) |
                                (((dd >>  3) & 1U) << 5) |
                                (((e >>  5) & 1U) << 6) |
                                (((e >>  6) & 1U) << 7));

    if (href != 0U) {
      hi_count++;
      if (seen_hi[v] == 0U) {
        seen_hi[v] = 1U;
        hi_distinct++;
      }
      if (hi_written < 16U) {
        cam_bus_hi_samples[hi_written++] = v;
      }
    } else {
      lo_count++;
      if (seen_lo[v] == 0U) {
        seen_lo[v] = 1U;
        lo_distinct++;
      }
      if (lo_written < 16U) {
        cam_bus_lo_samples[lo_written++] = v;
      }
    }

    if (href != prev_href) {
      href_edges++;
      prev_href = href;
    }
    if (vsync != prev_vsync) {
      vsync_edges++;
      prev_vsync = vsync;
    }
  }

  cam_bus_hi_count    = hi_count;
  cam_bus_lo_count    = lo_count;
  cam_bus_hi_distinct = hi_distinct;
  cam_bus_lo_distinct = lo_distinct;
  cam_href_edges      = href_edges;
  cam_vsync_edges     = vsync_edges;
"""

if old not in s:
    raise SystemExit("OLD SNIPPET NOT FOUND")

s = s.replace(old, new, 1)
open(p, "w", encoding="utf-8").write(s)
print("patched bsp_camera.c probe body")
