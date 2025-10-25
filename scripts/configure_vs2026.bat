:: --graphviz=build/erhe_cmake_dependencies.dot ^
:: --profiling-format=google-trace ^
:: --profiling-output=build/erhe_cmake_profiling.json ^

@echo off

setlocal

for /f "delims=" %%a in ('powershell -nologo -command "Get-Date -Format o"') do set "start=%%a"

cmake ^
 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
 -G "Visual Studio 18 2026" ^
 -A x64 ^
 -B build ^
 -S . ^
 -Wno-dev ^
 %* ^
 -DERHE_USE_PRECOMPILED_HEADERS=ON ^
 -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
 -DERHE_GLTF_LIBRARY=fastgltf ^
 -DERHE_GUI_LIBRARY=imgui ^
 -DERHE_PHYSICS_LIBRARY=jolt ^
 -DERHE_PROFILE_LIBRARY=tracy ^
 -DERHE_RAYTRACE_LIBRARY=bvh ^
 -DERHE_SVG_LIBRARY=plutosvg ^
 -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr ^
 -DERHE_USE_ASAN:BOOL=OFF

for /f "delims=" %%a in ('powershell -nologo -command "Get-Date -Format o"') do set "end=%%a"

for /f "delims=" %%a in ('powershell -nologo -command "$start=[datetime]::Parse('%start%'); $end=[datetime]::Parse('%end%'); ($end - $start).TotalSeconds"') do set duration=%%a

echo Configure completed in %duration% seconds
