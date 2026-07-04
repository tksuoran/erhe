:: --graphviz=build/erhe_cmake_dependencies.dot ^
:: --profiling-format=google-trace ^
:: --profiling-output=build/erhe_cmake_profiling.json ^

@echo off

setlocal

cmake ^
 -G "Visual Studio 18 2026" ^
 -A x64 ^
 -B build_vs2026_vulkan_asan ^
 -S . ^
 -Wno-dev ^
 %* ^
 -DERHE_USE_PRECOMPILED_HEADERS=ON ^
 -DERHE_FONT_RASTERIZATION_LIBRARY=none ^
 -DERHE_GLTF_LIBRARY=fastgltf ^
 -DERHE_GUI_LIBRARY=imgui ^
 -DERHE_GRAPHICS_API=vulkan ^
 -DERHE_NAVIGATION_LIBRARY=none ^
 -DERHE_PHYSICS_LIBRARY=none ^
 -DERHE_PROFILE_LIBRARY=none ^
 -DERHE_RAYTRACE_LIBRARY=bvh ^
 -DERHE_SVG_LIBRARY=none ^
 -DERHE_TEXT_LAYOUT_LIBRARY=none ^
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr ^
 -DERHE_USE_ASAN:BOOL=ON ^
 -DERHE_SPIRV=ON
