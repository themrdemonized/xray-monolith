@echo off
echo Compiling Fluid Graphics Shaders...

set GLSLANG="C:\VulkanSDK\1.4.335.0\Bin\glslangValidator.exe"

REM Raydata back (ray entry points)
%GLSLANG% -V raydata_back.vert -o raydata_back.vert.spv
if %errorlevel% neq 0 exit /b %errorlevel%

%GLSLANG% -V raydata_back.frag -o raydata_back.frag.spv
if %errorlevel% neq 0 exit /b %errorlevel%

REM Raydata front (ray exit points)
%GLSLANG% -V raydata_front.vert -o raydata_front.vert.spv
if %errorlevel% neq 0 exit /b %errorlevel%

%GLSLANG% -V raydata_front.frag -o raydata_front.frag.spv
if %errorlevel% neq 0 exit /b %errorlevel%

REM Fullscreen quad
%GLSLANG% -V fullscreen.vert -o fullscreen.vert.spv
if %errorlevel% neq 0 exit /b %errorlevel%

REM Raycast fog (volumetric raymarching)
%GLSLANG% -V raycast_fog.frag -o raycast_fog.frag.spv
if %errorlevel% neq 0 exit /b %errorlevel%

echo All graphics shaders compiled successfully!
pause
