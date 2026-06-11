#!/bin/bash

# Source Vulkan SDK environment if available.
# Respect an SDK already sourced in the current shell (VULKAN_SDK set).
# Otherwise pick the highest-versioned SDK under ~/VulkanSDK (glob order is lexicographic,
# which would otherwise pick e.g. 1.3.211 over 1.4.341).
if [ -z "${VULKAN_SDK}" ]; then
    latest_sdk_dir=""
    for sdk_dir in $(printf '%s\n' ~/VulkanSDK/*/ 2>/dev/null | sort -V); do
        if [ -f "${sdk_dir}setup-env.sh" ]; then
            latest_sdk_dir="${sdk_dir}"
        fi
    done
    if [ -n "${latest_sdk_dir}" ]; then
        echo "Sourcing Vulkan SDK: ${latest_sdk_dir}"
        source "${latest_sdk_dir}setup-env.sh"
    fi
else
    echo "Using Vulkan SDK already in environment: ${VULKAN_SDK}"
fi

cmake \
    -G "Xcode" \
    -B build_xcode_vulkan_minimal \
    -S . \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=1 \
    -Wno-dev \
    -DERHE_USE_PRECOMPILED_HEADERS=ON \
    -DERHE_FONT_RASTERIZATION_LIBRARY=none \
    -DERHE_GLTF_LIBRARY=none \
    -DERHE_GUI_LIBRARY=none \
    -DERHE_GRAPHICS_API=vulkan \
    -DERHE_NAVIGATION_LIBRARY=none \
    -DERHE_PHYSICS_LIBRARY=none\
    -DERHE_PROFILE_LIBRARY=none \
    -DERHE_RAYTRACE_LIBRARY=none \
    -DERHE_SVG_LIBRARY=none \
    -DERHE_TEXT_LAYOUT_LIBRARY=none \
    -DERHE_WINDOW_LIBRARY=sdl \
    -DERHE_XR_LIBRARY=none \
    -DERHE_USE_ASAN:BOOL=OFF \
    -DERHE_BUILD_TESTS=ON
