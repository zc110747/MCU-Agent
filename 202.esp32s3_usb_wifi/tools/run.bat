@echo off
rem ============================================================================
rem run.bat - ESP32-S3 USB Wi-Fi RNDIS 一键 编译+烧录+监视 (Windows)
rem
rem 端口: COM21 (CH343 下载口，可在下方 PORT 变量修改)
rem 用法: 双击运行  tools\run.bat  -> 自动编译、烧录到 COM21、打开串口监视
rem       按 Ctrl+] 退出监视 (monitor)
rem ============================================================================
setlocal

set "PORT=COM21"
rem IDF_PATH 可用环境变量覆盖；默认指向当前安装位置 D:\esp\esp-idf
set "IDF_PATH=%IDF_PATH%"
if "%IDF_PATH%"=="" set "IDF_PATH=D:\esp\esp-idf"
set "PROJECT_DIR=%~dp0.."

if not exist "%IDF_PATH%\export.bat" (
    echo [ERR] 未找到 %IDF_PATH%\export.bat
    pause
    exit /b 1
)

echo [INFO] 端口 = %PORT%
call "%IDF_PATH%\export.bat"
cd /d "%PROJECT_DIR%" || (echo [ERR] 无法进入工程目录 & pause & exit /b 1)

idf.py set-target esp32s3
if errorlevel 1 (echo [ERR] set-target 失败 & pause & exit /b 1)

echo [INFO] 编译 + 烧录到 %PORT% + 监视 ...
idf.py -p %PORT% flash monitor
if errorlevel 1 (echo [ERR] 烧录/监视失败 & pause & exit /b 1)

pause
