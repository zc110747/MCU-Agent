@echo off
rem ============================================================================
rem build_all.bat - ESP32-S3 USB Wi-Fi RNDIS 一键完整编译 (Windows)
rem
rem 前置: ESP-IDF 已安装 (默认 D:\esp\esp-idf，可用环境变量 IDF_PATH 覆盖)
rem       工具链路径由系统环境变量 IDF_TOOLS_PATH 提供 (安装时 export.bat 会设置)
rem 用法: 双击运行，或在 CMD/PowerShell 中执行  tools\build_all.bat
rem 说明: 自动加载 IDF 环境，指定目标为 esp32s3，执行完整编译 (idf.py build)。
rem       产物在 build\esp32s3\*.bin
rem ============================================================================
setlocal

rem IDF_PATH 可用环境变量覆盖；默认指向当前安装位置 D:\esp\esp-idf
set "IDF_PATH=%IDF_PATH%"
if "%IDF_PATH%"=="" set "IDF_PATH=D:\esp\esp-idf"
set "PROJECT_DIR=%~dp0.."

if not exist "%IDF_PATH%\export.bat" (
    echo [ERR] 未找到 %IDF_PATH%\export.bat
    echo [ERR] 请先安装 ESP-IDF 到 D:\software\esp32-tools\esp-idf
    pause
    exit /b 1
)

echo [INFO] IDF_PATH        = %IDF_PATH%
echo [INFO] IDF_TOOLS_PATH  = %IDF_TOOLS_PATH%
echo [INFO] PROJECT_DIR     = %PROJECT_DIR%

echo [INFO] 加载 ESP-IDF 环境 ...
call "%IDF_PATH%\export.bat"

cd /d "%PROJECT_DIR%" || (echo [ERR] 无法进入工程目录 & pause & exit /b 1)

echo [INFO] 指定目标芯片 esp32s3 ...
idf.py set-target esp32s3
if errorlevel 1 (
    echo [ERR] set-target 失败
    pause
    exit /b 1
)

echo [INFO] 完整编译中 ...
idf.py build
if errorlevel 1 (
    echo [ERR] 编译失败，请查看上方错误
    pause
    exit /b 1
)

echo [OK] 编译完成，产物: build\esp32s3\esp32s3.bin / partitions.bin / bootloader.bin
pause
