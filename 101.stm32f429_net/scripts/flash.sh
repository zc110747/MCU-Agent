#!/usr/bin/env bash
# flash.sh - 用 ST-Link (SWD) + OpenOCD 烧录 STM32F429IGT6
# 用法: ./scripts/flash.sh [elf_path]
set -euo pipefail

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# openocd.exe 是原生 Windows 程序，需要 Windows 风格路径 (E:/... 而非 /e/...)
PROJ_ROOT_WIN="$(cd "$PROJ_ROOT" && pwd -W)"
ELF="${1:-$PROJ_ROOT/build/stm32f429_net.elf}"
ELF_WIN="$(cd "$(dirname "$ELF")" && pwd -W)/$(basename "$ELF")"
OCD_CFG_WIN="$PROJ_ROOT_WIN/openocd/openocd.cfg"
OCD_BIN="/d/software/ST/OpenOCD/bin/openocd"

if [ ! -f "$ELF" ]; then
  echo "ERROR: ELF not found: $ELF" >&2
  exit 1
fi
if [ ! -x "$OCD_BIN" ]; then
  echo "ERROR: openocd not found at $OCD_BIN" >&2
  exit 1
fi

# 清理残留 openocd 进程，避免 LIBUSB_ERROR_ACCESS (USB 被占用)
echo "[flash] cleaning stale openocd processes..."
tasklist.exe 2>/dev/null | grep -i openocd >/dev/null && {
  taskkill.exe //F //IM openocd.exe >/dev/null 2>&1 || true
  sleep 1
} || echo "[flash] none running"

echo "[flash] programming $ELF ..."
"$OCD_BIN" -f "$OCD_CFG_WIN" \
  -c "init" \
  -c "halt" \
  -c "program \"$ELF_WIN\" verify reset exit"

echo "[flash] done."
