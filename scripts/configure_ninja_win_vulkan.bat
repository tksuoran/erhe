@echo off
setlocal

rem Locate VS 2026 (v18) with the C++ toolset via vswhere, the official
rem edition- and install-path-independent locator shipped by the VS installer.
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo vswhere.exe not found -- is Visual Studio installed?
    exit /b 1
)
set "VSINSTALL="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo No Visual Studio installation with C++ build tools found by vswhere.
    exit /b 1
)
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1

cmake ^
 -G Ninja ^
 -B build_ninja_win_vulkan ^
 -S . ^
 -Wno-dev ^
 -DCMAKE_BUILD_TYPE=Debug ^
 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
 %* ^
 -DERHE_USE_PRECOMPILED_HEADERS=ON ^
 -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
 -DERHE_GLTF_LIBRARY=fastgltf ^
 -DERHE_GUI_LIBRARY=imgui ^
 -DERHE_GRAPHICS_API=vulkan ^
 -DERHE_NAVIGATION_LIBRARY=none ^
 -DERHE_PHYSICS_LIBRARY=jolt ^
 -DERHE_PROFILE_LIBRARY=tracy ^
 -DERHE_RAYTRACE_LIBRARY=bvh ^
 -DERHE_SVG_LIBRARY=plutosvg ^
 -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr ^
 -DERHE_USE_ASAN:BOOL=OFF ^
 -DERHE_SPIRV=ON
