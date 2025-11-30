set(_target "erhe_graphics")
add_library(${_target})
add_library(erhe::graphics ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graphics/blit_command_encoder.cpp
    erhe_graphics/blit_command_encoder.hpp
    erhe_graphics/buffer.cpp
    erhe_graphics/buffer.hpp
    erhe_graphics/buffer_transfer_queue.cpp
    erhe_graphics/buffer_transfer_queue.hpp
    erhe_graphics/command_encoder.cpp
    erhe_graphics/command_encoder.hpp
    erhe_graphics/compute_command_encoder.cpp
    erhe_graphics/compute_command_encoder.hpp
    erhe_graphics/compute_pipeline_state.cpp
    erhe_graphics/compute_pipeline_state.hpp
    erhe_graphics/device.cpp
    erhe_graphics/device.hpp
    erhe_graphics/enums.cpp
    erhe_graphics/enums.hpp
    erhe_graphics/fragment_output.cpp
    erhe_graphics/fragment_output.hpp
    erhe_graphics/fragment_outputs.cpp
    erhe_graphics/fragment_outputs.hpp
    erhe_graphics/glsl_file_loader.cpp
    erhe_graphics/glsl_file_loader.hpp
    erhe_graphics/glsl_format_source.cpp
    erhe_graphics/glsl_format_source.hpp
    erhe_graphics/gpu_timer.cpp
    erhe_graphics/gpu_timer.hpp
    erhe_graphics/graphics_log.cpp
    erhe_graphics/graphics_log.hpp
    erhe_graphics/image_loader.hpp
    erhe_graphics/image_loader_wuffs.cpp
    erhe_graphics/image_loader_wuffs.hpp
    erhe_graphics/render_command_encoder.cpp
    erhe_graphics/render_command_encoder.hpp
    erhe_graphics/render_pass.cpp
    erhe_graphics/render_pass.hpp
    erhe_graphics/render_pipeline_state.cpp
    erhe_graphics/render_pipeline_state.hpp
    erhe_graphics/ring_buffer.cpp
    erhe_graphics/ring_buffer.hpp
    erhe_graphics/ring_buffer_range.cpp
    erhe_graphics/ring_buffer_range.hpp
    erhe_graphics/ring_buffer_client.cpp
    erhe_graphics/ring_buffer_client.hpp
    erhe_graphics/sampler.cpp
    erhe_graphics/sampler.hpp
    erhe_graphics/shader_monitor.cpp
    erhe_graphics/shader_monitor.hpp
    erhe_graphics/shader_resource.cpp
    erhe_graphics/shader_resource.hpp
    erhe_graphics/shader_stages.cpp
    erhe_graphics/shader_stages.hpp
    erhe_graphics/shader_stages_create_info.cpp
    erhe_graphics/span.hpp
    erhe_graphics/state/color_blend_state.cpp
    erhe_graphics/state/color_blend_state.hpp
    erhe_graphics/state/depth_stencil_state.cpp
    erhe_graphics/state/depth_stencil_state.hpp
    erhe_graphics/state/input_assembly_state.cpp
    erhe_graphics/state/input_assembly_state.hpp
    erhe_graphics/state/multisample_state.cpp
    erhe_graphics/state/multisample_state.hpp
    erhe_graphics/state/rasterization_state.cpp
    erhe_graphics/state/rasterization_state.hpp
    erhe_graphics/state/scissor_state.cpp
    erhe_graphics/state/scissor_state.hpp
    erhe_graphics/state/vertex_input_state.cpp
    erhe_graphics/state/vertex_input_state.hpp
    erhe_graphics/state/viewport_state.cpp
    erhe_graphics/state/viewport_state.hpp
    erhe_graphics/surface.cpp
    erhe_graphics/surface.hpp
    erhe_graphics/swapchain.cpp
    erhe_graphics/swapchain.hpp
    erhe_graphics/texture.cpp
    erhe_graphics/texture.hpp
    erhe_graphics/texture_heap.cpp
    erhe_graphics/texture_heap.hpp
)

if (ERHE_GRAPHICS_API_OPENGL)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/gl/gl_blit_command_encoder.cpp
        erhe_graphics/gl/gl_blit_command_encoder.hpp
        erhe_graphics/gl/gl_buffer.cpp
        erhe_graphics/gl/gl_buffer.hpp
        erhe_graphics/gl/gl_command_encoder.cpp
        erhe_graphics/gl/gl_command_encoder.hpp
        erhe_graphics/gl/gl_compute_command_encoder.cpp
        erhe_graphics/gl/gl_compute_command_encoder.hpp
        erhe_graphics/gl/gl_context_provider.cpp
        erhe_graphics/gl/gl_context_provider.hpp
        erhe_graphics/gl/gl_debug.cpp
        erhe_graphics/gl/gl_debug.hpp
        erhe_graphics/gl/gl_device.cpp
        erhe_graphics/gl/gl_device.hpp
        erhe_graphics/gl/gl_helpers.cpp
        erhe_graphics/gl/gl_helpers.hpp
        erhe_graphics/gl/gl_objects.cpp
        erhe_graphics/gl/gl_objects.hpp
        erhe_graphics/gl/gl_gpu_timer.cpp
        erhe_graphics/gl/gl_gpu_timer.hpp
        erhe_graphics/gl/gl_state_tracker.cpp
        erhe_graphics/gl/gl_state_tracker.hpp
        erhe_graphics/gl/gl_render_command_encoder.cpp
        erhe_graphics/gl/gl_render_command_encoder.hpp
        erhe_graphics/gl/gl_render_pass.cpp
        erhe_graphics/gl/gl_render_pass.hpp
        #erhe_graphics/gl/gl_renderbuffer.cpp
        #erhe_graphics/gl/gl_renderbuffer.hpp
        erhe_graphics/gl/gl_sampler.cpp
        erhe_graphics/gl/gl_sampler.hpp
        erhe_graphics/gl/gl_shader_stages.cpp
        erhe_graphics/gl/gl_shader_stages.hpp
        erhe_graphics/gl/gl_shader_stages_prototype.cpp
        erhe_graphics/gl/gl_surface.cpp
        erhe_graphics/gl/gl_surface.hpp
        erhe_graphics/gl/gl_swapchain.cpp
        erhe_graphics/gl/gl_swapchain.hpp
        erhe_graphics/gl/gl_texture.cpp
        erhe_graphics/gl/gl_texture.hpp
        erhe_graphics/gl/gl_texture_heap.cpp
        erhe_graphics/gl/gl_texture_heap.hpp
        erhe_graphics/gl/gl_vertex_input_state.cpp
        erhe_graphics/gl/gl_vertex_input_state.hpp
    )
    target_link_libraries(${_target} PRIVATE erhe::gl)

endif ()
if (ERHE_GRAPHICS_API_VULKAN)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/vulkan/vulkan_blit_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_blit_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_buffer.cpp
        erhe_graphics/vulkan/vulkan_buffer.hpp
        erhe_graphics/vulkan/vulkan_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_compute_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_compute_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_debug.cpp
        erhe_graphics/vulkan/vulkan_debug.hpp
        erhe_graphics/vulkan/vulkan_device.cpp
        erhe_graphics/vulkan/vulkan_device.hpp
        erhe_graphics/vulkan/vulkan_helpers.cpp
        erhe_graphics/vulkan/vulkan_helpers.hpp
        erhe_graphics/vulkan/vulkan_gpu_timer.cpp
        erhe_graphics/vulkan/vulkan_gpu_timer.hpp
        erhe_graphics/vulkan/vulkan_render_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_render_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_render_pass.cpp
        erhe_graphics/vulkan/vulkan_render_pass.hpp
        erhe_graphics/vulkan/vulkan_sampler.cpp
        erhe_graphics/vulkan/vulkan_sampler.hpp
        erhe_graphics/vulkan/vulkan_shader_stages.cpp
        erhe_graphics/vulkan/vulkan_shader_stages.hpp
        erhe_graphics/vulkan/vulkan_shader_stages_prototype.cpp
        erhe_graphics/vulkan/vulkan_surface.cpp
        erhe_graphics/vulkan/vulkan_surface.hpp
        erhe_graphics/vulkan/vulkan_swapchain.cpp
        erhe_graphics/vulkan/vulkan_swapchain.hpp
        erhe_graphics/vulkan/vulkan_texture.cpp
        erhe_graphics/vulkan/vulkan_texture.hpp
        erhe_graphics/vulkan/vulkan_texture_heap.cpp
        erhe_graphics/vulkan/vulkan_texture_heap.hpp
        erhe_graphics/vulkan/vulkan_vertex_input_state.cpp
        erhe_graphics/vulkan/vulkan_vertex_input_state.hpp
        erhe_graphics/vulkan/vma_usage.cpp
    )

    # get_target_property(ERHE_VMA_INCLUDES GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES)
    # get_target_property(ERHE_VMA_LIBS     GPUOpen::VulkanMemoryAllocator INTERFACE_LINK_LIBRARIES)
    # message(STATUS "GPUOpen::VulkanMemoryAllocator INTERFACE_INCLUDE_DIRECTORIES         = ${ERHE_VMA_INCLUDES}")
    # message(STATUS "GPUOpen::VulkanMemoryAllocator INTERFACE_LINK_LIBRARIES linked libs  = ${ERHE_VMA_LIBS}")

    target_compile_definitions(
        ${_target}
        PRIVATE
            VMA_STATIC_VULKAN_FUNCTIONS
            VMA_DYNAMIC_VULKAN_FUNCTIONS=0
	)

    target_link_libraries(
        ${_target}
        PRIVATE
            volk::volk
            GPUOpen::VulkanMemoryAllocator
    )
endif ()

if (ERHE_SPIRV)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/glsl_to_spirv.cpp
        erhe_graphics/glsl_to_spirv.hpp
    )
    target_link_libraries(${_target}
        PUBLIC
            glslang
            glslang::glslang-default-resource-limits
    )
endif()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        concurrentqueue
        erhe::configuration
        erhe::dataformat
        erhe::item
        erhe::window
        fmt::fmt
        glm::glm-header-only
        mango
    PRIVATE
        erhe::defer
        erhe::file
        erhe::log
        erhe::profile
        erhe::utility
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
