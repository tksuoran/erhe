@echo off

:: Install the built Android APK onto a connected device or emulator via adb.
::
:: Usage:
::     scripts\install_android.bat <mobile|quest> [debug|release] [run] [-s serial]
::
:: First positional argument selects the product flavor:
::     mobile  -> applicationId org.libsdl.app          (regular phone APK)
::     quest   -> applicationId org.libsdl.app.quest    (Meta Quest 3 APK)
::
:: Remaining arguments (any order):
::     debug | release   :: build type (default: debug)
::     run               :: launch the activity after install
::     -s <serial>       :: forwarded to adb to select a device

setlocal EnableDelayedExpansion

:: Capture the script's own directory before any `shift` (default `shift`
:: also moves %0, after which %~dp0 would resolve against cwd).
set "_script_dir=%~dp0"

:: --- Locate Android SDK -----------------------------------------------------
if "%ANDROID_HOME%"=="" (
    if exist "%LOCALAPPDATA%\Android\Sdk\" (
        set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    )
)
if "%ANDROID_HOME%"=="" (
    echo ERROR: ANDROID_HOME is not set and %%LOCALAPPDATA%%\Android\Sdk does not exist.
    echo Install Android Studio or set ANDROID_HOME to your SDK path.
    exit /b 1
)

set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
if not exist "%ADB%" (
    echo ERROR: adb.exe not found at %ADB%
    echo Install "Android SDK Platform-Tools" via Android Studio's SDK Manager.
    exit /b 1
)

:: --- Parse flavor argument --------------------------------------------------
set "_flavor=%~1"
if /i "%_flavor%"=="mobile" (
    set "_app_id=org.libsdl.app"
    goto :flavor_ok
)
if /i "%_flavor%"=="quest" (
    set "_app_id=org.libsdl.app.quest"
    goto :flavor_ok
)
echo ERROR: usage: %~nx0 ^<mobile^|quest^> [debug^|release] [run] [-s serial]
exit /b 1
:flavor_ok
shift

:: --- Parse remaining arguments ---------------------------------------------
set "_build_type=debug"
set "_run=0"
set "_serial="

:parse_args
if "%~1"=="" goto :args_done
if /i "%~1"=="debug" (
    set "_build_type=debug"
    shift
    goto :parse_args
)
if /i "%~1"=="release" (
    set "_build_type=release"
    shift
    goto :parse_args
)
if /i "%~1"=="run" (
    set "_run=1"
    shift
    goto :parse_args
)
if /i "%~1"=="-s" (
    if "%~2"=="" (
        echo ERROR: -s requires a device serial argument.
        exit /b 1
    )
    set "_serial=-s %~2"
    shift
    shift
    goto :parse_args
)
echo ERROR: Unknown argument: %~1
exit /b 1
:args_done

:: --- Locate APK -------------------------------------------------------------
set "_proj_root=%_script_dir%.."
set "_apk=%_proj_root%\android-project\app\build\outputs\apk\%_flavor%\%_build_type%\app-%_flavor%-%_build_type%.apk"

if not exist "%_apk%" (
    echo ERROR: APK not found: %_apk%
    echo Run scripts\build_android.bat %_flavor% first.
    exit /b 1
)

:: --- Verify a device is connected -------------------------------------------
"%ADB%" %_serial% get-state >nul 2>&1
if errorlevel 1 (
    echo ERROR: No device/emulator detected by adb.
    echo Connect a device with USB debugging enabled, or start an emulator.
    "%ADB%" devices
    exit /b 1
)

echo ADB     = %ADB%
echo Flavor  = %_flavor%
echo App id  = %_app_id%
echo APK     = %_apk%
if not "%_serial%"=="" echo Device  = %_serial%

:: -r: replace existing app, -g: grant runtime permissions, -d: allow downgrade
"%ADB%" %_serial% install -r -g -d "%_apk%"
if errorlevel 1 (
    echo Install FAILED.
    exit /b 1
)

if "%_run%"=="1" (
    echo Launching %_app_id%/org.libsdl.app.ErheActivity ...
    "%ADB%" %_serial% shell am start -n %_app_id%/org.libsdl.app.ErheActivity
    if errorlevel 1 (
        echo Launch FAILED.
        exit /b 1
    )
)

echo Done.
exit /b 0
