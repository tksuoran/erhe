@echo off
setlocal enabledelayedexpansion

::set BACKENDS=bvh tinybvh embree none
set BACKENDS=embree

set PASS_COUNT=0
set FAIL_COUNT=0

for %%B in (%BACKENDS%) do (
    echo.
    echo ========================================
    echo  Raytrace backend: %%B
    echo ========================================

    echo --- Configuring %%B ---
    cmake ^
     -G "Visual Studio 18 2026" ^
     -A x64 ^
     -B build_test_raytrace_%%B ^
     -S . ^
     -Wno-dev ^
     -DERHE_USE_PRECOMPILED_HEADERS=ON ^
     -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
     -DERHE_GLTF_LIBRARY=fastgltf ^
     -DERHE_GUI_LIBRARY=imgui ^
     -DERHE_GRAPHICS_LIBRARY=none ^
     -DERHE_PHYSICS_LIBRARY=jolt ^
     -DERHE_PROFILE_LIBRARY=none ^
     -DERHE_RAYTRACE_LIBRARY=%%B ^
     -DERHE_SVG_LIBRARY=plutosvg ^
     -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
     -DERHE_WINDOW_LIBRARY=none ^
     -DERHE_XR_LIBRARY=none ^
     -DERHE_USE_ASAN:BOOL=OFF ^
     -DERHE_SPIRV=OFF ^
     -DERHE_BUILD_TESTS=ON

    if errorlevel 1 (
        echo CONFIGURE FAILED for %%B
        set /a FAIL_COUNT+=1
    ) else (
        echo --- Building %%B ---
        cmake --build build_test_raytrace_%%B --target erhe_raytrace_tests --config Debug --parallel
        if errorlevel 1 (
            echo BUILD FAILED for %%B
            set /a FAIL_COUNT+=1
        ) else (
            echo --- Running tests for %%B ---
            build_test_raytrace_%%B\src\erhe\raytrace\test\Debug\erhe_raytrace_tests.exe
            if errorlevel 1 (
                echo SOME TESTS FAILED for %%B ^(expected for none backend^)
                set /a FAIL_COUNT+=1
            ) else (
                echo ALL TESTS PASSED for %%B
                set /a PASS_COUNT+=1
            )
        )
    )
)

echo.
echo ========================================
echo  Summary: !PASS_COUNT! passed, !FAIL_COUNT! failed
echo ========================================
