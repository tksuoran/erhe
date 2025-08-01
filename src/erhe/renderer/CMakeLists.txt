set(_target "erhe_renderer")
add_library(${_target})
add_library(erhe::renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_renderer/debug_renderer.cpp
    erhe_renderer/debug_renderer.hpp
    erhe_renderer/debug_renderer_bucket.cpp
    erhe_renderer/debug_renderer_bucket.hpp
    erhe_renderer/draw_indirect_buffer.cpp
    erhe_renderer/draw_indirect_buffer.hpp
    erhe_renderer/primitive_renderer.cpp
    erhe_renderer/primitive_renderer.hpp
    erhe_renderer/pipeline_renderpass.cpp
    erhe_renderer/pipeline_renderpass.hpp
    erhe_renderer/renderer_log.cpp
    erhe_renderer/renderer_log.hpp
    erhe_renderer/text_renderer.cpp
    erhe_renderer/text_renderer.hpp
)
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "jolt")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_renderer/jolt_debug_renderer.cpp
        erhe_renderer/jolt_debug_renderer.hpp
    )
    target_link_libraries(${_target} PUBLIC Jolt)
endif ()

target_link_libraries(
    ${_target}
    PUBLIC
        etl::etl
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::scene
        erhe::ui
    PRIVATE
        erhe::utility
        erhe::defer
        erhe::log
        erhe::profile
        erhe::verify
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

erhe_target_settings(${_target} "erhe")
