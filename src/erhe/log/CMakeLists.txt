set(_target "erhe_log")
add_library(${_target})
add_library(erhe::log ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_log/log.cpp
    erhe_log/log.hpp
    erhe_log/log_glm.hpp
    erhe_log/log_geogram.hpp
    erhe_log/timestamp.cpp
    erhe_log/timestamp.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        spdlog::spdlog_header_only
        glm::glm-header-only
        fmt::fmt
        geogram
    PRIVATE
        erhe::hash
        erhe::configuration
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
