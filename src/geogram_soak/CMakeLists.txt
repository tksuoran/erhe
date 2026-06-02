# geogram_soak -- headless soak harness for the intermittent main-loop hang.
# See doc/intermittent_main_loop_hang.md and src/geogram_soak/main.cpp.
#
# Built on desktop AND Android (arm64) deliberately: the corruption it hunts is
# ARM-only, so the useful runs are on-device via `adb shell`. It links only
# erhe::geometry (+ taskflow), so it pulls in no rendering / SDL / Vulkan / XR.
set(_target "geogram_soak")
add_executable(${_target} main.cpp)

target_link_libraries(${_target}
    PRIVATE
        erhe::geometry
        erhe::log
        erhe::math
        Taskflow
        rapidjson
)

erhe_target_settings(${_target} "erhe")
