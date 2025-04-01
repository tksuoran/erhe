@rem TODO Re-enable -DERHE_USE_PRECOMPILED_HEADERS=ON
@rem      https://github.com/tksuoran/erhe/issues/139

cmake ^
 -G "Visual Studio 17 2022" ^
 -A x64 ^
 -B build ^
 -S . ^
 --graphviz=erhe_cmake_dependencies.dot ^
 -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ^
 -Wno-dev ^
 -DERHE_USE_PRECOMPILED_HEADERS=OFF ^
 -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
 -DERHE_GLTF_LIBRARY=fastgltf ^
 -DERHE_GUI_LIBRARY=imgui ^
 -DERHE_PHYSICS_LIBRARY=jolt ^
 -DERHE_PROFILE_LIBRARY=tracy ^
 -DERHE_RAYTRACE_LIBRARY=bvh ^
 -DERHE_SVG_LIBRARY=lunasvg ^
 -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
 -DERHE_WINDOW_LIBRARY=sdl ^
 -DERHE_XR_LIBRARY=openxr
