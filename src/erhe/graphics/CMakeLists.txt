set(_target "erhe_graphics")
add_library(${_target})
add_library(erhe::graphics ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_graphics/command_encoder.cpp
    erhe_graphics/command_encoder.hpp
    erhe_graphics/device.cpp
    erhe_graphics/device.hpp
    erhe_graphics/enums.cpp
    erhe_graphics/enums.hpp
    erhe_graphics/graphics_log.cpp
    erhe_graphics/graphics_log.hpp
    erhe_graphics/render_command_encoder.cpp
    erhe_graphics/render_command_encoder.hpp
    erhe_graphics/render_pass.cpp
    erhe_graphics/render_pass.hpp
    erhe_graphics/render_pipeline_state.cpp
    erhe_graphics/render_pipeline_state.hpp
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
)

if (ERHE_GRAPHICS_API_VULKAN)
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_graphics/vulkan/vulkan_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_debug.cpp
        erhe_graphics/vulkan/vulkan_debug.hpp
        erhe_graphics/vulkan/vulkan_device.cpp
        erhe_graphics/vulkan/vulkan_device.hpp
        erhe_graphics/vulkan/vulkan_helpers.cpp
        erhe_graphics/vulkan/vulkan_helpers.hpp
        erhe_graphics/vulkan/vulkan_render_command_encoder.cpp
        erhe_graphics/vulkan/vulkan_render_command_encoder.hpp
        erhe_graphics/vulkan/vulkan_render_pass.cpp
        erhe_graphics/vulkan/vulkan_render_pass.hpp
        erhe_graphics/vulkan/vulkan_surface.cpp
        erhe_graphics/vulkan/vulkan_surface.hpp
        erhe_graphics/vulkan/vulkan_swapchain.cpp
        erhe_graphics/vulkan/vulkan_swapchain.hpp
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

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        erhe::dataformat
        erhe::window
        fmt::fmt
        glm::glm-header-only
    PRIVATE
        erhe::defer
        erhe::log
        erhe::utility
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
