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

cmake --build build_ninja_win_vulkan --target %1
