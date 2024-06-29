set(_target "erhe_renderer")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_library(${_target})
add_library(erhe::renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_renderer/buffer_writer.cpp
    erhe_renderer/buffer_writer.hpp
    erhe_renderer/draw_indirect_buffer.cpp
    erhe_renderer/draw_indirect_buffer.hpp
    erhe_renderer/line_renderer.cpp
    erhe_renderer/line_renderer.hpp
    erhe_renderer/multi_buffer.cpp
    erhe_renderer/multi_buffer.hpp
    erhe_renderer/pipeline_renderpass.cpp
    erhe_renderer/pipeline_renderpass.hpp
    erhe_renderer/renderer_log.cpp
    erhe_renderer/renderer_log.hpp
    erhe_renderer/text_renderer.cpp
    erhe_renderer/text_renderer.hpp
)
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::graphics
        erhe::math
        erhe::primitive
        erhe::scene
        erhe::ui
    PRIVATE
        erhe::concurrency
        erhe::gl
        erhe::log
        erhe::profile
        erhe::verify
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
