@echo off
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" exit /b 0
echo Open an x64 Native Tools Command Prompt for VS 2022 and retry.
exit /b 1
