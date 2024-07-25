set(_target "erhe_graphics")
add_library(${_target})
add_library(erhe::graphics ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graphics/buffer_transfer_queue.cpp
    erhe_graphics/buffer_transfer_queue.hpp
    erhe_graphics/buffer.cpp
    erhe_graphics/buffer.hpp
    erhe_graphics/debug.cpp
    erhe_graphics/debug.hpp
    erhe_graphics/fragment_output.cpp
    erhe_graphics/fragment_output.hpp
    erhe_graphics/fragment_outputs.cpp
    erhe_graphics/fragment_outputs.hpp
    erhe_graphics/framebuffer.cpp
    erhe_graphics/framebuffer.hpp
    erhe_graphics/gl_context_provider.cpp
    erhe_graphics/gl_context_provider.hpp
    erhe_graphics/gl_objects.cpp
    erhe_graphics/gl_objects.hpp
    erhe_graphics/gpu_timer.cpp
    erhe_graphics/gpu_timer.hpp
    erhe_graphics/graphics_log.cpp
    erhe_graphics/graphics_log.hpp
    erhe_graphics/instance.cpp
    erhe_graphics/instance.hpp
    erhe_graphics/opengl_state_tracker.cpp
    erhe_graphics/opengl_state_tracker.hpp
    erhe_graphics/pipeline.cpp
    erhe_graphics/pipeline.hpp
    erhe_graphics/image_loader_wuffs.cpp
    erhe_graphics/image_loader_wuffs.hpp
    erhe_graphics/image_loader.hpp
    erhe_graphics/renderbuffer.cpp
    erhe_graphics/renderbuffer.hpp
    erhe_graphics/sampler.cpp
    erhe_graphics/sampler.hpp
    erhe_graphics/shader_monitor.cpp
    erhe_graphics/shader_monitor.hpp
    erhe_graphics/shader_resource.cpp
    erhe_graphics/shader_resource.hpp
    erhe_graphics/shader_stages_create_info.cpp
    erhe_graphics/shader_stages_prototype.cpp
    erhe_graphics/shader_stages.cpp
    erhe_graphics/shader_stages.hpp
    erhe_graphics/span.hpp
    erhe_graphics/state/color_blend_state.cpp
    erhe_graphics/state/color_blend_state.hpp
    erhe_graphics/state/depth_stencil_state.cpp
    erhe_graphics/state/depth_stencil_state.hpp
    erhe_graphics/state/input_assembly_state.cpp
    erhe_graphics/state/input_assembly_state.hpp
    erhe_graphics/state/rasterization_state.cpp
    erhe_graphics/state/rasterization_state.hpp
    erhe_graphics/state/vertex_input_state.cpp
    erhe_graphics/state/vertex_input_state.hpp
    erhe_graphics/state/viewport_state.cpp
    erhe_graphics/state/viewport_state.hpp
    erhe_graphics/texture.cpp
    erhe_graphics/texture.hpp
    erhe_graphics/vertex_attribute_mapping.cpp
    erhe_graphics/vertex_attribute_mapping.hpp
    erhe_graphics/vertex_attribute_mappings.cpp
    erhe_graphics/vertex_attribute_mappings.hpp
    erhe_graphics/vertex_attribute.cpp
    erhe_graphics/vertex_attribute.hpp
    erhe_graphics/vertex_format.cpp
    erhe_graphics/vertex_format.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        concurrentqueue
        erhe::configuration
        erhe::dataformat
        erhe::gl
        erhe::item
        erhe::window
        fmt::fmt
        glm::glm-header-only
        mango
        #### wuffs
        #### glslang
    PRIVATE
        erhe::bit
        erhe::defer
        erhe::file
        erhe::log
        erhe::profile
        erhe::verify
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
