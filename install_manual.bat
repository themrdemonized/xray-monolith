@echo off
echo Installing Vulkan SDK...
echo.
echo This will launch the installer with administrator rights.
echo Please follow the installation wizard.
echo.
pause

"%TEMP%\VulkanSDK-Installer.exe"

echo.
echo Installation completed!
echo IMPORTANT: Close this window and restart your terminal/IDE
echo.
pause
