#!/bin/bash

# Linux Ninja configure for the Vulkan backend.
# OpenGL variant: configure_ninja_linux.sh
# ERHE_SPIRV is forced ON for Vulkan by CMakeLists.txt, so it is not passed here.

mkdir -p build/ninja-vulkan
cmake \
    -G "Ninja" \
    -B build/ninja-vulkan \
    -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype \
    -DERHE_GLTF_LIBRARY=fastgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_GRAPHICS_API=vulkan \
    -DERHE_PHYSICS_LIBRARY=jolt \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=bvh \
    -DERHE_SVG_LIBRARY=plutosvg \
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz \
    -DERHE_WINDOW_LIBRARY=glfw \
    -DERHE_XR_LIBRARY=none
