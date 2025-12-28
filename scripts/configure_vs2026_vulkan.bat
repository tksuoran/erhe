
cmake ^
 -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ^
 -G "Visual Studio 18 2026" ^
 -A x64 ^
 -B build ^
 -S . ^
 -Wno-dev ^
 %* ^
 -DERHE_USE_PRECOMPILED_HEADERS=ON ^
 -DERHE_GRAPHICS_LIBRARY=vulkan ^
 -DERHE_WINDOW_LIBRARY=sdl
