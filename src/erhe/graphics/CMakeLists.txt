set(_target "erhe_graphics")
add_library(${_target})
add_library(erhe::graphics ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graphics/buffer_transfer_queue.cpp
    erhe_graphics/buffer_transfer_queue.hpp
    erhe_graphics/debug.cpp
    erhe_graphics/debug.hpp
    erhe_graphics/fragment_output.cpp
    erhe_graphics/fragment_output.hpp
    erhe_graphics/fragment_outputs.cpp
    erhe_graphics/fragment_outputs.hpp
    erhe_graphics/gpu_timer.cpp
    erhe_graphics/gpu_timer.hpp
    erhe_graphics/graphics_log.cpp
    erhe_graphics/graphics_log.hpp
    erhe_graphics/png_loader.hpp
    erhe_graphics/shader_monitor.cpp
    erhe_graphics/shader_monitor.hpp
    erhe_graphics/shader_resource.cpp
    erhe_graphics/shader_resource.hpp
    erhe_graphics/shader_stages_create_info.cpp
    erhe_graphics/shader_stages_prototype.cpp
    erhe_graphics/shader_stages.cpp
    erhe_graphics/shader_stages.hpp
    erhe_graphics/span.hpp
    erhe_graphics/vertex_attribute_mapping.cpp
    erhe_graphics/vertex_attribute_mapping.hpp
    erhe_graphics/vertex_attribute_mappings.cpp
    erhe_graphics/vertex_attribute_mappings.hpp
    erhe_graphics/vertex_attribute.cpp
    erhe_graphics/vertex_attribute.hpp
    erhe_graphics/vertex_format.cpp
    erhe_graphics/vertex_format.hpp
)
if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/png_loader_mango_spng.cpp
        erhe_graphics/png_loader_mango_spng.hpp
    )
elseif (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/png_loader_fpng.cpp
        erhe_graphics/png_loader_fpng.hpp
    )
else()
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/png_loader_none.cpp
        erhe_graphics/png_loader_none.hpp
    )
endif()
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        concurrentqueue
        erhe::configuration
        #erhe::gl
        erhe::item
        IGLLibrary
        erhe::window
        fmt::fmt
        glm::glm
        glslang
        Microsoft.GSL::GSL
    PRIVATE
        erhe::bit
        erhe::defer
        erhe::file
        erhe::log
        erhe::profile
        erhe::verify
)
if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target} PUBLIC mango spng)
endif()
if (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    target_link_libraries(${_target} PUBLIC fpng)
endif()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
