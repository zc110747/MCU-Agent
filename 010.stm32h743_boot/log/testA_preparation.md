# Test A 升级流程 —— 准备与验证状态

生成时间: 2026-08-25
会话目标: 真机验证 安全升级 v1.0.0.5 -> v1.0.0.6

## 1. 升级包（已生成于 dist/）

- `dist/stm32h7_test.bin` — 46,164 B，版本槽 @0x1000 = [1,0,0,6]
  （由 `test_app/build/stm32h7_app.bin` 经 `tools/gen_upgrade.py` 生成）
- `dist/verify.json`:
  ```json
  { "name": "stm32h7_test.bin", "len": 46164,
    "HMAC-SHA256": "bac00f874db9f97c134560fdd004f642baf90db36585da477d823fc2ea2cfab4",
    "version": "1.0.0.6" }
  ```

## 2. 交叉校验（tools/verify_hmac.py）—— 全 PASS

- 工具密钥 == Bootloader `BOOT_HMAC_KEY`（b"STM32H7BootKey2026#U-Disk", 25 B）
- `verify.json` version == .bin 版本槽 @0x1000 (1.0.0.6)
- HMAC 实现均为 RFC2104 标准（密钥不足 64B 零填充），主机签名 == 固件验签

## 3. 升级代码接线

- `app/main.c:68` 调用 `BSP_Upgrade_Check()`，位于 USB/跳转逻辑之前
- 流程（app/upgrade.c）：挂载 FatFs → 读 verify.json(name/len/HMAC/version) → 打开同名 bin 比对长度 → 流式 HMAC 比对 → 版本不同则擦 App 扇区1..14 → 流式编程+读回校验 → 写配置区(magic/len/version/hmac/crc) → 卸载
- 任一校验失败均在擦写前 abort，已运行 App 不受影响

## 4. 真机串口实测（Golden path）

- 复位后 Bootloader 日志：
  ```
  QSPI initialised (HAL indirect mode)
  [BOOT] checking QSPI volume for upgrade package...
  [FS ] FAT volume mounted
  [UPG] scanning QSPI volume for upgrade package...
  [UPG] no verify.json (0x04) - nothing to upgrade
  [FS ] unmounted (USB MSC takes over)
  [BOOT] app image OK app v1.0.0.5
  [BOOT] app ready - 8 s jump window (plug USB to enter U-disk)
  [BOOT] USB connected in window -> U-disk mode
  ```
- 结论：配置校验通过；因 OTG_FS USB 连用户机器，正确进入 U-disk 模式（不跳转，符合设计）。

## 5. openocd QSPI 直写排查（受阻，供参考）

- `flash probe stm32h7x.qspi`：`No QSPI, no OCTOSPI at 0x52005000` → 配 QUADSPI CR/DCR+EN 后 `timeout`
- 已修正 GPIO：PF6 漏配（MODER 0x002A8000 -> 0x002AA000）
- `reset run` 让固件自初始化 QSPI 后 `halt` + ABORT 仍 timeout
- 无 mkfs.fat/mtools，无法生成 FAT 镜像直烧
- **结论**：放弃 openocd 直写；Test A 走 U 盘路径（用户机器拷包 + 沙箱看 COM19）

## 6. 用户执行步骤（板子当前处于 U-disk 模式，用户机器已挂载）

1. 拷 `dist/stm32h7_test.bin`（文件名保持）与 `dist/verify.json` 到 U 盘根目录
2. 安全弹出 U 盘
3. 复位板子（想看新 App 运行则拔掉 OTG_FS USB，使 8s 窗口超时跳转）
4. 沙箱验收：
   - `python tools/verify_serial.py --port COM19 --expect upgrade`
     （期望 HMAC-SHA256 verified OK + upgrade SUCCESS）
   - `python tools/verify_serial.py --port COM19 --expect jump`
     （期望 app image OK v1.0.0.6 + TEST APP v1.0.0.6 + app alive @1Hz）
