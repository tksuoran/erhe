@rem rd /S /Q build
cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -Thost=x64 ^
    -B build ^
    -S . ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ^
    -Wno-dev ^
    -DERHE_PNG_LIBRARY=none ^
    -DERHE_FONT_RASTERIZATION_LIBRARY=none ^
    -DERHE_TEXT_LAYOUT_LIBRARY=none ^
    -DERHE_WINDOW_LIBRARY=glfw ^
    -DERHE_RAYTRACE_LIBRARY=none ^
    -DERHE_PHYSICS_LIBRARY=none ^
    -DERHE_PROFILE_LIBRARY=none ^
    -DERHE_XR_LIBRARY=OpenXR
