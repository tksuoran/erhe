set(_target "erhe_graphics")
add_library(${_target})
add_library(erhe::graphics ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    buffer.cpp
    buffer.hpp
    buffer_transfer_queue.cpp
    buffer_transfer_queue.hpp
    configuration.cpp
    configuration.hpp
    debug.cpp
    debug.hpp
    fragment_output.cpp
    fragment_output.hpp
    fragment_outputs.cpp
    fragment_outputs.hpp
    framebuffer.cpp
    framebuffer.hpp
    gl_objects.cpp
    gl_objects.hpp
    log.cpp
    log.hpp
    opengl_state_tracker.cpp
    opengl_state_tracker.hpp
    pipeline.cpp
    pipeline.hpp
    png_loader.hpp
    renderbuffer.cpp
    renderbuffer.hpp
    sampler.cpp
    sampler.hpp
    shader_resource.cpp
    shader_resource.hpp
    shader_stages_create_info.cpp
    shader_stages_prototype.cpp
    shader_stages.cpp
    shader_stages.hpp
    span.hpp
    texture.cpp
    texture.hpp
    vertex_attribute_mapping.cpp
    vertex_attribute_mapping.hpp
    vertex_attribute_mappings.cpp
    vertex_attribute_mappings.hpp
    vertex_attribute.cpp
    vertex_attribute.hpp
    vertex_format.cpp
    vertex_format.hpp
    state/color_blend_state.cpp
    state/color_blend_state.hpp
    state/depth_stencil_state.cpp
    state/depth_stencil_state.hpp
    state/input_assembly_state.cpp
    state/input_assembly_state.hpp
    state/rasterization_state.cpp
    state/rasterization_state.hpp
    state/vertex_input_state.cpp
    state/vertex_input_state.hpp
    state/viewport_state.cpp
    state/viewport_state.hpp
)

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        png_loader_mango.cpp
        png_loader_mango.hpp
    )
elseif (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        png_loader_fpng.cpp
        png_loader_fpng.hpp
    )
else()
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        png_loader_none.cpp
        png_loader_none.hpp
    )
endif()

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        erhe::components
        erhe::gl
        fmt::fmt
        ghc_filesystem
        glm::glm
        optional_lite
        Microsoft.GSL::GSL
    PRIVATE
        erhe::log
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

if (${ERHE_PNG_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target} PUBLIC mango)
endif()
if (${ERHE_PNG_LIBRARY} STREQUAL "fpng")
    target_link_libraries(${_target} PUBLIC fpng)
endif()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
