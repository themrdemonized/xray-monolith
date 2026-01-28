@echo off
echo Starting game with Vulkan renderer...
echo Logs will be saved to game_console.log
echo.

cd /d "%~dp0"

rem Create log file
echo Game Launch Log > game_console.log
echo ================ >> game_console.log
echo. >> game_console.log

rem Run game and capture output
"_build\__game\bin_dbg\VerifiedDX11.exe" -nointro -nosound -windowed >> game_console.log 2>&1

echo.
echo Game has exited. Check game_console.log for output.
pause
