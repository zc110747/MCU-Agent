#!/usr/bin/env bash
# Dump the firmware's debug counters over SWD without halting the CPU.
#
#   ./debug/telemetry.sh          # single snapshot
#   ./debug/telemetry.sh 3        # snapshot, wait 3 s, snapshot again (shows deltas)
#
# Symbol addresses are resolved from the ELF every run, so this keeps working
# after the linker moves things around.

set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ELF="${ROOT}/build/stm32h743_uvc.elf"
CFG="${ROOT}/debug/openocd.cfg"

[ -f "$ELF" ] || { echo "no ELF at $ELF - build first" >&2; exit 1; }

# The GNU Arm toolchain and OpenOCD are native Windows binaries under Git Bash
# and choke on "/d/..." style paths, so hand them Windows paths instead.
if command -v cygpath >/dev/null 2>&1; then
  ELF="$(cygpath -w "$ELF")"
  CFG="$(cygpath -m "$CFG")"
fi

SYMS=(
  g_boot_stage g_loop_count
  g_fault_id g_fault_cfsr g_fault_hfsr
  g_cam_status g_cam_id
  cam_frame_count cam_error_count cam_start_count cam_start_fail_count
  usb_mounted usb_mount_count usb_suspend_count usb_commit_count
  uvc_frames_sent uvc_frames_dropped
  uvc_xfer_started uvc_xfer_rejected uvc_tx_timeouts uvc_cap_timeouts
  uvc_stream_poll_true uvc_stream_poll_false
  uvc_state
)

# ---- resolve symbol -> address -------------------------------------------
declare -A ADDR
NM_OUT="$(arm-none-eabi-nm "$ELF")"
for s in "${SYMS[@]}"; do
  a="$(echo "$NM_OUT" | awk -v n="$s" '$3 == n { print $1; exit }')"
  [ -n "$a" ] && ADDR[$s]="0x$a"
done

# ---- build one OpenOCD batch that reads everything ------------------------
# Reverse map address -> symbol; OpenOCD echoes the address with every read, so
# we can match the results back without relying on ordering.
declare -A BY_ADDR
CMDS=(-c init)
ORDER=()
for s in "${SYMS[@]}"; do
  [ -n "${ADDR[$s]:-}" ] || continue
  BY_ADDR[${ADDR[$s]}]="$s"
  CMDS+=(-c "mdw ${ADDR[$s]} 1")
  ORDER+=("$s")
done
CMDS+=(-c shutdown)

# OpenOCD logs everything - including mdw results - on stderr, hence the 2>&1.
read_once() {
  local raw
  raw="$(openocd -f "$CFG" "${CMDS[@]}" 2>&1)"

  local a v s
  while read -r a v; do
    a="${a%:}"
    s="${BY_ADDR[$a]:-}"
    [ -n "$s" ] && echo "$s 0x$v"
  done < <(echo "$raw" | grep -oE '^0x[0-9a-f]+: [0-9a-f]+')
}

snap1="$(read_once)"
if [ -z "$snap1" ]; then
  echo "target not responding - check the ST-Link cable" >&2
  exit 1
fi

DELAY="${1:-0}"
if [ "$DELAY" != "0" ]; then
  sleep "$DELAY"
  snap2="$(read_once)"
fi

# ---- pretty print ---------------------------------------------------------
decode_boot() {
  case "$1" in
    0) echo "reset vector / pre-main" ;;  1) echo "MPU+cache done" ;;
    2) echo "clock done" ;;               3) echo "LED done" ;;
    4) echo "camera probed" ;;            5) echo "USB GPIO done" ;;
    6) echo "tusb_init done" ;;           7) echo "uvc_app_init done" ;;
    8) echo "main loop running" ;;        *) echo "?" ;;
  esac
}
decode_cam() {
  case "$1" in
    0x00000000) echo "CAM_OK" ;;          0x000000ff) echo "CAM_ERR_I2C" ;;
    0x000000fe) echo "CAM_ERR_ID" ;;      0x000000fd) echo "CAM_ERR_SENSOR" ;;
    0x000000fc) echo "CAM_ERR_DCMI" ;;    *) echo "?" ;;
  esac
}
decode_state() {
  local v="$1" out=""
  (( v & 0x01 )) && out="$out streaming"
  (( v & 0x02 )) && out="$out tx_busy"
  (( v & 0x04 )) && out="$out cap_busy"
  (( v & 0x08 )) && out="$out frame_ready"
  (( v & 0x10 )) && out="$out cam_ok"
  out="$out cap_idx=$(( (v >> 8) & 0xF )) tx_idx=$(( (v >> 12) & 0xF ))"
  echo "${out# }"
}
decode_fault() {
  case "$1" in
    0x00000000) echo "none" ;;            0x00000001) echo "NMI" ;;
    0x00000002) echo "HARDFAULT" ;;       0x00000003) echo "MEMMANAGE" ;;
    0x00000004) echo "BUSFAULT" ;;        0x00000005) echo "USAGEFAULT" ;;
    *) echo "?" ;;
  esac
}

lookup() { echo "$1" | awk -v n="$2" '$1 == n { print $2; exit }'; }

printf '%-22s %-12s' "SYMBOL" "VALUE"
[ "$DELAY" != "0" ] && printf '%-12s %-8s' "AFTER+${DELAY}s" "DELTA"
printf '\n'
printf -- '--------------------------------------------------------------------------\n'

for name in "${ORDER[@]}"; do
  val="$(lookup "$snap1" "$name")"
  [ -n "$val" ] || continue

  # g_cam_status is a 1-byte enum; mdw returns the whole word, so mask it off.
  if [ "$name" = "g_cam_status" ]; then
    val="$(printf '0x%08x' $(( val & 0xFF )))"
  fi

  printf '%-22s %-12s' "$name" "$val"

  if [ "$DELAY" != "0" ]; then
    v2="$(lookup "$snap2" "$name")"
    [ "$name" = "g_cam_status" ] && v2="$(printf '0x%08x' $(( v2 & 0xFF )))"
    d=$(( v2 - val ))
    printf '%-12s %-8s' "$v2" "$(printf '%+d' "$d")"
  fi

  case "$name" in
    g_boot_stage) printf '<- %s' "$(decode_boot $((val)))" ;;
    g_cam_status) printf '<- %s' "$(decode_cam "$val")" ;;
    g_fault_id)   printf '<- %s' "$(decode_fault "$val")" ;;
    uvc_state)    printf '<- %s' "$(decode_state $((val)))" ;;
  esac
  printf '\n'
done
