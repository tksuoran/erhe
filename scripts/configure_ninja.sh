#!/bin/bash

# LD=ld.lld-12 CC=clang-12 CXX=clang++-12 scripts/configure_ninja.sh

cmake \
    -G "Ninja" \
    -B build \
    -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -DCMAKE_EXE_LINKER_FLAGS=-fuse-ld=lld \
    -DCMAKE_SHARED_LINKER_FLAGS=-fuse-ld=lld \
    -Wno-dev \
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype \
    -DERHE_GLTF_LIBRARY=cgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_PHYSICS_LIBRARY=jolt \
    -DERHE_PNG_LIBRARY=mango \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=bvh \
    -DERHE_SVG_LIBRARY=lunasvg \
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz \
    -DERHE_WINDOW_LIBRARY=glfw \
    -DERHE_XR_LIBRARY=none
