@echo off
setlocal

rem Usage:
rem   configure_ninja_win_vulkan.bat [Debug|Release] [extra cmake args...]
rem
rem Ninja is a single-configuration generator, so the build type is baked into
rem the build directory at configure time and each configuration needs its own:
rem
rem   Debug   (default) -> build_ninja_win_vulkan
rem   Release           -> build_ninja_win_vulkan_release
rem
rem Everything that is not the configuration keyword is forwarded to cmake, in
rem the same position %* held before -- i.e. before this script's own -D
rem options, which therefore still win on a conflict (cmake takes the last
rem value given for a repeated -D). Same convention as the other configure
rem scripts here.

set "ERHE_CONFIG=Debug"
set "ERHE_BUILD_DIR=build_ninja_win_vulkan"
set "ERHE_EXTRA_ARGS="

:parse_args
if "%~1"=="" goto args_parsed
if /i "%~1"=="Debug" (
    set "ERHE_CONFIG=Debug"
    set "ERHE_BUILD_DIR=build_ninja_win_vulkan"
    shift
    goto parse_args
)
if /i "%~1"=="Release" (
    set "ERHE_CONFIG=Release"
    set "ERHE_BUILD_DIR=build_ninja_win_vulkan_release"
    shift
    goto parse_args
)
rem %1 rather than %~1: keep any quoting the caller used around -D values.
set "ERHE_EXTRA_ARGS=%ERHE_EXTRA_ARGS% %1"
shift
goto parse_args
:args_parsed

echo Configuring %ERHE_CONFIG% build in %ERHE_BUILD_DIR%

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
rem VsDevCmd.bat pushd's into the VS Installer directory and runs bare
rem "vswhere.exe", relying on cmd's current-directory lookup. Shells that set
rem NoDefaultCurrentDirectoryInExePath (Git for Windows, agent shells) disable
rem that lookup, yielding a noisy (harmless) "'vswhere.exe' is not recognized"
rem error and an unset VSCMD_VER. Appending the Installer directory to the
rem setlocal-scoped PATH lets that call resolve either way; -no_logo drops the
rem banner.
set "PATH=%PATH%;%ProgramFiles(x86)%\Microsoft Visual Studio\Installer"
call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64 -no_logo
if errorlevel 1 exit /b 1

cmake ^
 -G Ninja ^
 -B %ERHE_BUILD_DIR% ^
 -S . ^
 -Wno-dev ^
 -DCMAKE_BUILD_TYPE=%ERHE_CONFIG% ^
 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
 %ERHE_EXTRA_ARGS% ^
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
 -DERHE_VOXEL_LIBRARY=openvdb ^
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr ^
 -DERHE_USE_ASAN:BOOL=OFF ^
 -DERHE_SPIRV=ON
