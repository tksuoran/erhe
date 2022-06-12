@rem rd /S /Q build
@rem -D CMAKE_GENERATOR_INSTANCE="C:/Program Files/Microsoft Visual Studio/2022/Community" ^

cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -Thost=x64 ^
    -B build ^
    -S . ^
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 ^
    -Wno-dev ^
    -DERHE_PHYSICS_LIBRARY=jolt ^
    -DERHE_RAYTRACE_LIBRARY=bvh ^
    -DERHE_GLTF_LIBRARY=none ^
    -DERHE_SVG_LIBRARY=lunasvg
