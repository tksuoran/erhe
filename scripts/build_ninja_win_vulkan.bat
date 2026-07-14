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

cmake --build build_ninja_win_vulkan --target %1
