@echo off
REM 一键通过 CMSIS-DAP 探针烧录 F429 demo
REM 适用：F429 上可能运行会进低功耗睡眠、关闭调试时钟的固件。
REM 用法：直接运行本脚本即可（序列内含 hold-reset + DBGMCU 保持，无需手动 POR）。
REM
REM 关键序列（已验证）：
REM   init -> 写 DBGMCU_CR=0x7(保持调试时钟) -> reset run -> halt -> 擦写校验
REM 这样即使旧固件会睡死，也能抢在调试域关闭前 halt 住并烧录。

setlocal
set PATH=E:\support_tools\arm-gnu-toolchain-15.3.rel1-mingw-w64-x86_64-arm-none-eabi\bin;C:\Software\msys2\mingw64\bin;D:\software\ST\OpenOCD\bin;%PATH%

cd /d %~dp0\..
set IMAGE=f429_demo/f429_demo.elf
openocd -f openocd/stm32f429_cmsisdap.cfg ^
  -c "adapter speed 100" ^
  -c "init" ^
  -c "mww 0xe0042004 0x7" ^
  -c "reset run" ^
  -c "halt" ^
  -c "flash write_image erase %IMAGE%" ^
  -c "flash verify_image %IMAGE%" ^
  -c "reset halt" ^
  -c "echo *** F429 flash done ***" ^
  -c "exit"
endlocal
