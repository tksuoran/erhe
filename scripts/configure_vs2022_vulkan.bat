
cmake ^
 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
 -G "Visual Studio 17 2022" ^
 -A x64 ^
 -B build ^
 -S . ^
 -Wno-dev ^
 %* ^
 -DERHE_USE_PRECOMPILED_HEADERS=ON ^
 -DERHE_GRAPHICS_LIBRARY=vulkan ^
 -DERHE_WINDOW_LIBRARY=sdl
