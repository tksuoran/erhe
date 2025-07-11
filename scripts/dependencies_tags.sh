#!/bin/bash

REPOS=(
    "BrunoLevy/geogram"
    "fmtlib/fmt"
    "gabime/spdlog"
    "cameron314/concurrentqueue"
    "jarro2783/cxxopts"
    "ETLCPP/etl"
    "g-truc/glm"
    "KhronosGroup/glslang"
    "nlohmann/json"
    "libsdl-org/sdl"
    "glfw/glfw"
    "simdjson/simdjson"
    "taskflow/taskflow"
    "google/wuffs"
    "sammycage/plutosvg"
    "spnda/fastgltf"
    "NVIDIA/NVTX"
    "wolfpld/tracy"
    "freetype/freetype"
    "harfbuzz/harfbuzz"
    "embree/embree"
    "madmann91/bvh"
    "jrouwe/JoltPhysics"
    "KhronosGroup/OpenXR-SDK"
)

printf "%-28s | %-15s %-15s %-15s\n" "Repository" "Tag 1" "Tag 2" "Tag 3"
echo "---------------------------------------------------------------------------------------"

for REPO in "${REPOS[@]}"; do
    TAGS=($(gh api "repos/$REPO/tags" --jq '.[].name' | head -n 3))

    while [ "${#TAGS[@]}" -lt 3 ]; do
        TAGS+=(" ")
    done

    printf "%-28s | %-15s %-15s %-15s\n" "$REPO" "${TAGS[0]}" "${TAGS[1]}" "${TAGS[2]}"
done
