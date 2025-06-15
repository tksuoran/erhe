#!/bin/bash

# LD=ld.lld-15 CC=clang-15 CXX=clang++-15 scripts/configure_ninja.sh
#-DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
#-DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \

mkdir -p build/ninja
cmake \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -G "Xcode" \
    -B build/xcode \
    -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype \
    -DERHE_GLTF_LIBRARY=fastgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_PHYSICS_LIBRARY=jolt \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=bvh \
    -DERHE_SVG_LIBRARY=lunasvg \
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz \
    -DERHE_WINDOW_LIBRARY=sdl \
    -DERHE_XR_LIBRARY=none
