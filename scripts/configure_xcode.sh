#!/bin/bash

# LD=ld.lld-15 CC=clang-15 CXX=clang++-15 scripts/configure_ninja.sh
#-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
#-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \

mkdir -p build/ninja
cmake \
    -G "Xcode" \
    -B build/xcode \
    -S . \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_USE_PRECOMPILED_HEADERS=ON \
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype \
    -DERHE_GLTF_LIBRARY=fastgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_GRAPHICS_LIBRARY=opengl \
    -DERHE_PHYSICS_LIBRARY=none \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=none \
    -DERHE_SVG_LIBRARY=plutosvg \
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz \
    -DERHE_WINDOW_LIBRARY=sdl \
    -DERHE_XR_LIBRARY=none \
    -DERHE_USE_ASAN:BOOL=OFF \
    -DERHE_SPIRV=OFF \
    -DERHE_BUILD_TESTS=ON
