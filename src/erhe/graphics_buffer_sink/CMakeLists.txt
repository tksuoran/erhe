set(_target "erhe_graphics_buffer_sink")
add_library(${_target})
add_library(erhe::graphics_buffer_sink ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graphics_buffer_sink/graphics_buffer_sink.hpp
    erhe_graphics_buffer_sink/graphics_buffer_sink.cpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
    target_link_libraries(${_target} PUBLIC erhe_pch)
endif ()

erhe_target_settings(${_target})
target_link_libraries(${_target}
    PUBLIC
        erhe::graphics
        erhe::primitive
    PRIVATE
        erhe::profile
        erhe::verify
)

set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
