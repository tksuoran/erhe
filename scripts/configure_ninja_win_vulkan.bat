@echo off
setlocal

call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64 -host_arch=amd64
if errorlevel 1 exit /b 1

cmake ^
 -G Ninja ^
 -B build_ninja_win_vulkan ^
 -S . ^
 -Wno-dev ^
 -DCMAKE_BUILD_TYPE=Debug ^
 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON ^
 %* ^
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
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr ^
 -DERHE_USE_ASAN:BOOL=OFF ^
 -DERHE_SPIRV=ON
