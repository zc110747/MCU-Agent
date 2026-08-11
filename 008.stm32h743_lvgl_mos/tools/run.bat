@echo off
cd /d "%~dp0"
if exist "bin\Release\net9.0-windows\publish\NesPadTool.exe" (
  start "" "bin\Release\net9.0-windows\publish\NesPadTool.exe"
) else (
  dotnet run -c Release
)
