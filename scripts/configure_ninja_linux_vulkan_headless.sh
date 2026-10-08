#!/bin/bash

# Linux Ninja configure for the Vulkan backend without a window library
# (ERHE_WINDOW_LIBRARY=none): the editor and the GPU tests run without a
# display. Pass -DERHE_BUILD_TESTS=ON to build the tests.
# OpenGL variant: configure_ninja_linux_opengl.sh
# ERHE_SPIRV is forced ON for Vulkan by CMakeLists.txt, so it is not passed here.

mkdir -p build_ninja_linux_vulkan_headless
cmake \
    -G "Ninja" \
    -B build_ninja_linux_vulkan_headless \
    -S . \
    "$@" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_FONT_RASTERIZATION_LIBRARY=freetype \
    -DERHE_GLTF_LIBRARY=fastgltf \
    -DERHE_GUI_LIBRARY=imgui \
    -DERHE_GRAPHICS_API=vulkan \
    -DERHE_NAVIGATION_LIBRARY=none \
    -DERHE_PHYSICS_LIBRARY=jolt \
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=bvh \
    -DERHE_SVG_LIBRARY=plutosvg \
    -DERHE_TEXT_LAYOUT_LIBRARY=harfbuzz \
    -DERHE_USD_LIBRARY=lightusd \
    -DERHE_WINDOW_LIBRARY=none \
    -DERHE_XR_LIBRARY=none
