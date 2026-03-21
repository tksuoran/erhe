@echo off

setlocal

set BUILD_DIR=build_tests_asan
set CONFIG=Debug

if not exist %BUILD_DIR% (
    echo Build directory %BUILD_DIR% not found. Run configure_tests_asan.bat first.
    exit /b 1
)

echo === Building tests ===
cmake --build %BUILD_DIR% --config %CONFIG% --target erhe_item_tests erhe_smoke
if %errorlevel% neq 0 (
    echo Build failed.
    exit /b %errorlevel%
)

echo.
echo === Running unit tests ===
ctest --test-dir %BUILD_DIR% -C %CONFIG% --output-on-failure
if %errorlevel% neq 0 (
    echo Tests failed.
    exit /b %errorlevel%
)

echo.
echo === All tests passed ===
