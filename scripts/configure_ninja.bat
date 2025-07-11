@rem    -DCMAKE_C_COMPILER="C:\\Program Files\\LLVM\\bin\\clang.exe" ^
@rem    -DCMAKE_CXX_COMPILER="C:\\Program Files\\LLVM\\bin\\clang++.exe" ^

set CC="C:\\Program Files\\LLVM\\bin\\clang.exe"
set CXX="C:\\Program Files\\LLVM\\bin\\clang++.exe"

cmake ^
    -G "Ninja" ^
    -B build_ninja ^
    -S . ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ^
    -Wno-dev ^
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype ^
    -DERHE_GLTF_LIBRARY=fastgltf ^
    -DERHE_GUI_LIBRARY=imgui ^
    -DERHE_PHYSICS_LIBRARY=none ^
    -DERHE_PROFILE_LIBRARY=none ^
    -DERHE_RAYTRACE_LIBRARY=bvh ^
    -DERHE_SVG_LIBRARY=plutosvg ^
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz ^
    -DERHE_WINDOW_LIBRARY=glfw ^
    -DERHE_XR_LIBRARY=openxr
