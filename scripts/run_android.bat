@echo off

:: Build, install, and launch the editor APK on a connected device in one
:: shot. Uses Gradle's `install<Variant>` task which compiles, packages,
:: and `adb install`s in a single invocation, then `am start`s the
:: activity through the launcher intent so Horizon recognizes the quest
:: flavor as immersive-VR.
::
:: Usage:
::     scripts\run_android.bat <mobile|quest> [debug|release] [-s serial]
::
:: Defaults to debug if no build type is given.
::
:: Notes:
::   - release builds use the debug keystore (per app/build.gradle); not
::     suitable for store submission, fine for local performance testing.
::   - When multiple devices are attached, pass `-s <serial>` to target
::     a specific one.

setlocal EnableDelayedExpansion

:: Capture the script's own directory before any `shift` (default `shift`
:: also moves %0, after which %~dp0 would resolve against cwd).
set "_script_dir=%~dp0"

:: --- Parse flavor argument --------------------------------------------------
set "_flavor=%~1"
if /i "%_flavor%"=="mobile" (
    set "_flavor_cap=Mobile"
    set "_app_id=org.libsdl.app"
    goto :flavor_ok
)
if /i "%_flavor%"=="quest" (
    set "_flavor_cap=Quest"
    set "_app_id=org.libsdl.app.quest"
    goto :flavor_ok
)
echo ERROR: usage: %~nx0 ^<mobile^|quest^> [debug^|release] [-s serial]
exit /b 1
:flavor_ok
shift

:: --- Parse build type argument (optional) -----------------------------------
set "_build_type=debug"
set "_build_type_cap=Debug"
if /i "%~1"=="debug" (
    set "_build_type=debug"
    set "_build_type_cap=Debug"
    shift
) else if /i "%~1"=="release" (
    set "_build_type=release"
    set "_build_type_cap=Release"
    shift
)

:: --- Parse optional -s serial -----------------------------------------------
set "_serial="
if /i "%~1"=="-s" (
    if "%~2"=="" (
        echo ERROR: -s requires a device serial argument.
        exit /b 1
    )
    set "_serial=-s %~2"
    shift
    shift
)

if not "%~1"=="" (
    echo ERROR: unexpected argument: %~1
    echo usage: %~nx0 ^<mobile^|quest^> [debug^|release] [-s serial]
    exit /b 1
)

:: --- Locate Android SDK -----------------------------------------------------
if "%ANDROID_HOME%"=="" (
    if exist "%LOCALAPPDATA%\Android\Sdk\" (
        set "ANDROID_HOME=%LOCALAPPDATA%\Android\Sdk"
    )
)
if "%ANDROID_HOME%"=="" (
    echo ERROR: ANDROID_HOME is not set and %%LOCALAPPDATA%%\Android\Sdk does not exist.
    exit /b 1
)
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

set "ADB=%ANDROID_HOME%\platform-tools\adb.exe"
if not exist "%ADB%" (
    echo ERROR: adb.exe not found at %ADB%
    exit /b 1
)

:: --- Locate JDK 21 ----------------------------------------------------------
if "%JAVA_HOME%"=="" (
    if exist "C:\Program Files\Android\Android Studio\jbr\bin\java.exe" (
        set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
    )
)
if "%JAVA_HOME%"=="" (
    echo ERROR: JAVA_HOME is not set and Android Studio's JBR was not found.
    exit /b 1
)

:: --- Gradle: build + install ------------------------------------------------
set "_proj_root=%_script_dir%.."
set "_gradle_dir=%_proj_root%\android-project"
if not exist "%_gradle_dir%\gradlew.bat" (
    echo ERROR: %_gradle_dir%\gradlew.bat not found.
    exit /b 1
)

set "_gradle_task=install%_flavor_cap%%_build_type_cap%"

echo Flavor      = %_flavor%
echo Build type  = %_build_type%
echo App id      = %_app_id%
echo Gradle task = %_gradle_task%
if not "%_serial%"=="" echo Device      = %_serial%

:: ANDROID_SERIAL is honored by AGP's install task when set.
if not "%_serial%"=="" (
    for /f "tokens=2" %%s in ("%_serial%") do set "ANDROID_SERIAL=%%s"
)

call "%_gradle_dir%\gradlew.bat" -p "%_gradle_dir%" --no-daemon --console=plain %_gradle_task%
set "_rc=%ERRORLEVEL%"

if not "%_rc%"=="0" (
    echo Build/install FAILED, exit code %_rc%
    exit /b %_rc%
)

echo Build + install completed.

:: --- Launch via the LAUNCHER+VR intent so Horizon enters immersive ----------
echo Launching %_app_id%/org.libsdl.app.SDLActivity ...
"%ADB%" %_serial% shell am start -W -a android.intent.action.MAIN -c android.intent.category.LAUNCHER -c com.oculus.intent.category.VR -n %_app_id%/org.libsdl.app.SDLActivity
if errorlevel 1 (
    echo Launch FAILED.
    exit /b 1
)

echo Done.
exit /b 0
