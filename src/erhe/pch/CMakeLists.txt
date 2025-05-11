set(_target "erhe_pch")
add_library(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_pch/erhe_pch.cpp
)
target_include_directories(${_target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(erhe_pch PUBLIC
    [[erhe_pch/erhe_windows.hpp]]
    <glm/glm.hpp>
    <glm/gtc/packing.hpp>
    <glm/gtc/quaternion.hpp>
    <glm/gtc/quaternion.hpp>
    <glm/gtc/type_ptr.hpp>
    <fmt/format.h>
    <spdlog/spdlog.h>
    <span>
    <algorithm>
    <cassert>
    <cctype>
    <chrono>
    <cmath>
    <cstdint>
    <filesystem>
    <functional>
    <limits>
    <memory>
    <mutex>
    <optional>
    <utility>
    <sstream>
    <string>
    <string_view>
    <thread>
    <typeinfo>
    <unordered_map>
    <utility>
    <vector>
)

# Need to depend on spdlog to ensure compile settings in PCH match
target_link_libraries(${_target}
    PUBLIC
        #spdlog::spdlog
        spdlog::spdlog_header_only
        glm::glm-header-only
    PRIVATE
        fmt::fmt
)

erhe_target_settings(${_target} "erhe")


