@echo off
echo Compiling 3D Fluid shaders...

set GLSLANG="C:\VulkanSDK\1.4.335.0\Bin\glslangValidator.exe"
set SHADER_DIR=%~dp0

if not exist %GLSLANG% (
    echo ERROR: glslangValidator not found at %GLSLANG%
    echo Please install Vulkan SDK or update GLSLANG path
    exit /b 1
)

echo.
echo ========== Compute Shaders ==========
echo.

%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\advect.comp.spv" "%SHADER_DIR%compute\fluid\advect.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\advect_velocity.comp.spv" "%SHADER_DIR%compute\fluid\advect_velocity.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\advect_bfecc.comp.spv" "%SHADER_DIR%compute\fluid\advect_bfecc.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\vorticity.comp.spv" "%SHADER_DIR%compute\fluid\vorticity.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\confinement.comp.spv" "%SHADER_DIR%compute\fluid\confinement.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\divergence.comp.spv" "%SHADER_DIR%compute\fluid\divergence.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\jacobi.comp.spv" "%SHADER_DIR%compute\fluid\jacobi.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\project.comp.spv" "%SHADER_DIR%compute\fluid\project.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\emitter_gaussian.comp.spv" "%SHADER_DIR%compute\fluid\emitter_gaussian.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\emitter_draught.comp.spv" "%SHADER_DIR%compute\fluid\emitter_draught.comp"
%GLSLANG% -V -o "%SHADER_DIR%compute\fluid\obstacles.comp.spv" "%SHADER_DIR%compute\fluid\obstacles.comp"

echo.
echo ========== Graphics Shaders ==========
echo.

%GLSLANG% -V -S vert -o "%SHADER_DIR%fluid\raydata_back.vert.spv" "%SHADER_DIR%fluid\raydata_back.vert"
%GLSLANG% -V -S frag -o "%SHADER_DIR%fluid\raydata_back.frag.spv" "%SHADER_DIR%fluid\raydata_back.frag"
%GLSLANG% -V -S vert -o "%SHADER_DIR%fluid\raydata_front.vert.spv" "%SHADER_DIR%fluid\raydata_front.vert"
%GLSLANG% -V -S frag -o "%SHADER_DIR%fluid\raydata_front.frag.spv" "%SHADER_DIR%fluid\raydata_front.frag"
%GLSLANG% -V -S vert -o "%SHADER_DIR%fluid\fullscreen.vert.spv" "%SHADER_DIR%fluid\fullscreen.vert"
%GLSLANG% -V -S frag -o "%SHADER_DIR%fluid\raycast_fog.frag.spv" "%SHADER_DIR%fluid\raycast_fog.frag"

echo.
echo Compilation complete!
echo.
pause
