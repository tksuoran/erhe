set(_target "erhe_graphics")

set(source_list
    buffer.cpp
    buffer.hpp
    configuration.cpp
    configuration.hpp
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
    vertex_stream_binding.cpp
    vertex_stream_binding.hpp
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

if (${ERHE_PNG_LIBRARY} STREQUAL "Mango")
    list(APPEND source_list
         png_loader_mango.cpp
         png_loader_mango.hpp
    )
else()
    list(APPEND source_list
        png_loader_none.cpp
        png_loader_none.hpp
    )
endif()

add_library(${_target} ${source_list})

target_include_directories(${_target}
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(${_target}
    PUBLIC
        erhe_gl
        erhe_log
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

if (${ERHE_PNG_LIBRARY} STREQUAL "Mango")
    target_link_libraries(${_target} PUBLIC mango)
endif()

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
