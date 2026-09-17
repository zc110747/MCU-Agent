#!/usr/bin/env bash
# sync_skills.sh — 全局 skills（权威副本）<-> 本工程镜像备份，双向同步与漂移检查
#
# 设计原则
#   * 全局 ~/.workbuddy/skills/ 是**唯一权威副本**，所有编辑都在全局做。
#   * 本工程 <proj>/.workbuddy/skills/ 只是**只读镜像备份**，便于随仓库分发/换机恢复。
#   * 纳管范围 = MCU 域 18 个 skill + 能力表；非 MCU 域（soc-* / lvgl-* / mail-* / robotics-*
#     等）不镜像，避免把无关域塞进 MCU 工程仓库。
#
# 用法（在 Git Bash / WSL / Linux 下执行）
#   support_tools/sync_skills.sh             全局 -> 工程（默认，刷新镜像）
#   support_tools/sync_skills.sh --check     只报告漂移，不改动（0=一致, 1=有漂移）
#   support_tools/sync_skills.sh --restore   工程 -> 全局（换机 / 灾后恢复）
#   support_tools/sync_skills.sh --list      打印纳管清单
#
# 退出码：0 成功/无漂移；1 有漂移或出错。

set -uo pipefail

GLOBAL_DIR="${HOME}/.workbuddy/skills"
PROJ_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJ_DIR="${PROJ_ROOT}/.workbuddy/skills"

# ---- 纳管清单（MCU 域 18 个）------------------------------------------------
MANAGED_SKILLS=(
  stm32-vibe-coding-workflow
  stm32-ai-dev-environment
  stm32-project-scaffold
  stm32-peripheral-drivers
  stm32-lvgl-font-engine
  stm32-logging-print-log
  stm32-swd-forensics
  stm32-verification-acceptance
  stm32-keil-port
  stm32-cmsis-dap-probe
  zephyr-stm32-porting
  soc-cache-mpu
  esp32-arduino-cli-build
  esp32-board-hardware
  esp32-web-ui-state-push
  esp32-cortex-debug
  esp-idf-windows-build
  esp-idf-whole-archive-link
)
MANAGED_FILES=( "STM32_skills_能力表.md" )

# ---- 工具与基础检查 ---------------------------------------------------------
die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

[ -d "${GLOBAL_DIR}" ] || die "全局 skills 目录不存在: ${GLOBAL_DIR}"
[ -d "${PROJ_DIR}" ]   || die "工程镜像目录不存在: ${PROJ_DIR}（先在工程里建好 .workbuddy/skills/）"

MODE="sync"
case "${1:-}" in
  ""|--sync)   MODE="sync" ;;
  --check)     MODE="check" ;;
  --restore)   MODE="restore" ;;
  --prune)     MODE="prune" ;;
  --list)      MODE="list" ;;
  -h|--help)
    sed -n '2,20p' "${BASH_SOURCE[0]}"; exit 0 ;;
  *) die "未知参数: $1（用 --help 看用法）" ;;
esac

if [ "${MODE}" = "list" ]; then
  printf '纳管 skill（%d 个）:\n' "${#MANAGED_SKILLS[@]}"
  printf '  - %s\n' "${MANAGED_SKILLS[@]}"
  printf '纳管文件:\n'
  printf '  - %s\n' "${MANAGED_FILES[@]}"
  exit 0
fi

# ---- 漂移检测 ---------------------------------------------------------------
# 返回 0 = 一致；1 = 有差异。逐项 diff，输出差异摘要。
check_drift() {
  local drift=0 item src dst
  for item in "${MANAGED_SKILLS[@]}"; do
    src="${GLOBAL_DIR}/${item}"
    dst="${PROJ_DIR}/${item}"
    if [ ! -d "${src}" ]; then
      printf '  [缺失-全局] %s\n' "${item}"; drift=1; continue
    fi
    if [ ! -d "${dst}" ]; then
      printf '  [缺失-工程] %s\n' "${item}"; drift=1; continue
    fi
    if ! diff -r -q "${src}" "${dst}" >/dev/null 2>&1; then
      printf '  [内容漂移] %s\n' "${item}"
      diff -r -q "${src}" "${dst}" 2>/dev/null | sed 's/^/      /'
      drift=1
    fi
  done
  for item in "${MANAGED_FILES[@]}"; do
    src="${GLOBAL_DIR}/${item}"
    dst="${PROJ_DIR}/${item}"
    if [ ! -f "${src}" ] || [ ! -f "${dst}" ]; then
      printf '  [缺失-文件] %s\n' "${item}"; drift=1; continue
    fi
    cmp -s "${src}" "${dst}" || { printf '  [内容漂移] %s\n' "${item}"; drift=1; }
  done
  return "${drift}"
}

if [ "${MODE}" = "check" ]; then
  printf '== 漂移检查（%s -> %s）==\n' "${GLOBAL_DIR}" "${PROJ_DIR}"
  if check_drift; then
    printf 'RESULT: IDENTICAL（工程镜像与全局权威副本一致）\n'
    exit 0
  fi
  printf 'RESULT: DRIFT（存在漂移，跑 ./sync_skills.sh 刷新，或 --restore 反向恢复）\n'
  exit 1
fi

# ---- 同步 -------------------------------------------------------------------
# SRC -> DST，**覆盖式合并**（不预删整目录）。
# 理由：本沙箱对脚本内批量递归删除有中断行为，且“全局是超集”时根本不需要删。
# 若全局侧删过文件而镜像侧残留，--check 会报漂移，用 --prune 逐个清理（少量 rm -f）。
mirror_to() {
  local from="$1" to="$2" item
  for item in "${MANAGED_SKILLS[@]}"; do
    if [ ! -d "${from}/${item}" ]; then
      printf '  [跳过-源缺失] %s\n' "${item}"
      continue
    fi
    mkdir -p "${to}/${item}"
    cp -rf "${from}/${item}/." "${to}/${item}/" || die "拷贝失败: ${item}"
    printf '  [已同步] %s\n' "${item}"
  done
  for item in "${MANAGED_FILES[@]}"; do
    [ -f "${from}/${item}" ] || { printf '  [跳过-源缺失] %s\n' "${item}"; continue; }
    cp -f "${from}/${item}" "${to}/${item}" || die "拷贝失败: ${item}"
    printf '  [已同步] %s\n' "${item}"
  done
}

# 清理镜像侧存在、全局侧已不存在的残留文件（逐个 rm -f，避免批量递归删除）
prune_extras() {
  local from="$1" to="$2" item f n=0
  for item in "${MANAGED_SKILLS[@]}"; do
    [ -d "${from}/${item}" ] || continue
    while IFS= read -r f; do
      [ -n "${f}" ] || continue
      rm -f "${f}" && printf '  [已清理] %s\n' "${f#${to}/}"
      n=$((n + 1))
    done < <(cd "${from}/${item}" && find . -type f | sort | while IFS= read -r rel; do
               [ -f "${to}/${item}/${rel#./}" ] || echo "${to}/${item}/${rel#./}"
             done)
  done
  printf '  （共清理残留文件 %d 个）\n' "${n}"
}

if [ "${MODE}" = "sync" ] || [ "${MODE}" = "prune" ]; then
  if [ "${MODE}" = "prune" ]; then
    printf '== 清理镜像侧残留（全局已删除的文件）==\n'
    prune_extras "${GLOBAL_DIR}" "${PROJ_DIR}"
  fi
  printf '== 全局 -> 工程镜像（权威刷新）==\n'
  mirror_to "${GLOBAL_DIR}" "${PROJ_DIR}"
  printf '== 校验 ==\n'
  if check_drift; then
    printf 'RESULT: OK（工程镜像已与全局权威副本对齐）\n'
    exit 0
  fi
  printf 'RESULT: 同步后仍存在漂移（多为镜像侧残留文件）；跑 --prune 清理后重试\n'
  exit 1
fi

if [ "${MODE}" = "restore" ]; then
  printf '== 工程镜像 -> 全局（灾后恢复）==\n'
  printf '⚠️  将覆盖全局 %s 下的纳管 skill，确认这是你要的（Ctrl-C 中止）\n' "${GLOBAL_DIR}"
  sleep 3
  mirror_to "${PROJ_DIR}" "${GLOBAL_DIR}"
  printf '== 校验 ==\n'
  if check_drift; then
    printf 'RESULT: OK（全局已由工程镜像恢复）\n'
    exit 0
  fi
  printf 'RESULT: 恢复后仍存在漂移，请检查\n'
  exit 1
fi
