@echo off

:: Build the Android APK (arm64-v8a, Vulkan-only) via Gradle, which drives
:: erhe's top-level CMake under externalNativeBuild.
::
:: Usage:
::     scripts\build_android.bat <mobile|quest> [extra gradle args]
::
:: First positional argument selects the product flavor:
::     mobile  -> assembleMobileDebug (regular Android phone APK)
::     quest   -> assembleQuestDebug  (Meta Quest 3 APK)
::
:: Extra arguments are forwarded to gradlew, e.g.:
::     scripts\build_android.bat mobile assembleMobileRelease
::     scripts\build_android.bat quest clean assembleQuestDebug
::     scripts\build_android.bat quest assembleQuestDebug --info

setlocal

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
if not exist "%ANDROID_HOME%\platform-tools\" (
    echo ERROR: ANDROID_HOME=%ANDROID_HOME% does not look like a valid SDK
    echo - no platform-tools subdirectory.
    exit /b 1
)
set "ANDROID_SDK_ROOT=%ANDROID_HOME%"

:: --- Locate JDK 21 ----------------------------------------------------------
:: Gradle 8.12 (bundled wrapper) does not support JDK 25; use JDK 21 from
:: Android Studio's bundled JetBrains Runtime by default.
if "%JAVA_HOME%"=="" (
    if exist "C:\Program Files\Android\Android Studio\jbr\bin\java.exe" (
        set "JAVA_HOME=C:\Program Files\Android\Android Studio\jbr"
    )
)
if "%JAVA_HOME%"=="" (
    echo ERROR: JAVA_HOME is not set and Android Studio's JBR was not found.
    echo Set JAVA_HOME to a JDK 21 installation.
    exit /b 1
)
if not exist "%JAVA_HOME%\bin\java.exe" (
    echo ERROR: JAVA_HOME=%JAVA_HOME% does not contain bin\java.exe
    exit /b 1
)

:: --- Parse flavor argument --------------------------------------------------
set "_flavor=%~1"
if /i "%_flavor%"=="mobile" (
    set "_default_task=assembleMobileDebug"
    set "_apk_dir=mobile\debug"
    set "_apk_name=app-mobile-debug.apk"
    goto :flavor_ok
)
if /i "%_flavor%"=="quest" (
    set "_default_task=assembleQuestDebug"
    set "_apk_dir=quest\debug"
    set "_apk_name=app-quest-debug.apk"
    goto :flavor_ok
)
echo ERROR: usage: %~nx0 ^<mobile^|quest^> [extra gradle args]
exit /b 1
:flavor_ok
shift

:: --- Run Gradle -------------------------------------------------------------
set "_proj_root=%_script_dir%.."
set "_gradle_dir=%_proj_root%\android-project"

if not exist "%_gradle_dir%\gradlew.bat" (
    echo ERROR: %_gradle_dir%\gradlew.bat not found.
    exit /b 1
)

:: Forward any remaining args verbatim as the gradle task list
set "_tasks="
:collect_args
if "%~1"=="" goto :args_done
if defined _tasks (
    set "_tasks=%_tasks% %1"
) else (
    set "_tasks=%1"
)
shift
goto :collect_args
:args_done
if not defined _tasks set "_tasks=%_default_task%"

echo ANDROID_HOME = %ANDROID_HOME%
echo JAVA_HOME    = %JAVA_HOME%
echo Gradle dir   = %_gradle_dir%
echo Flavor       = %_flavor%
echo Tasks        = %_tasks%

call "%_gradle_dir%\gradlew.bat" -p "%_gradle_dir%" --no-daemon --console=plain %_tasks%
set "_rc=%ERRORLEVEL%"

if not "%_rc%"=="0" goto :failed

echo Build completed
set "_apk_path=%_gradle_dir%\app\build\outputs\apk\%_apk_dir%\%_apk_name%"
if exist "%_apk_path%" echo APK: %_apk_path%
exit /b 0

:failed
echo Build FAILED, exit code %_rc%
exit /b %_rc%
