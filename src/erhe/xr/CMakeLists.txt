set(_target "erhe_xr")
add_library(${_target})
add_library(erhe::xr ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_xr/xr_action.cpp
    erhe_xr/xr_action.hpp
)
if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_xr/headset.cpp
        erhe_xr/headset.hpp
        erhe_xr/xr_action.cpp
        erhe_xr/xr_action.hpp
        erhe_xr/xr_instance.cpp
        erhe_xr/xr_instance.hpp
        erhe_xr/xr_log.cpp
        erhe_xr/xr_log.hpp
        erhe_xr/xr_quad_layer.cpp
        erhe_xr/xr_quad_layer.hpp
        erhe_xr/xr_session.cpp
        erhe_xr/xr_session.hpp
        erhe_xr/xr_swapchain_image.cpp
        erhe_xr/xr_swapchain_image.hpp
        erhe_xr/xr.cpp
        erhe_xr/xr.hpp
    )
endif ()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

# Headset_config codegen. Headset_config lives here so Xr_session and other
# erhe::xr code can include it directly. The serialize/deserialize .cpp files
# are compiled in the editor target (alongside other simdjson users) to avoid
# ABI mismatch from differing arch flags -- only the struct headers are
# generated into this library.
set(_xr_config_defs "${CMAKE_CURRENT_SOURCE_DIR}/definitions")
set(_xr_config_out  "${CMAKE_CURRENT_BINARY_DIR}/erhe_xr/generated")

erhe_codegen_generate(
    TARGET          ${_target}
    DEFINITIONS_DIR "${_xr_config_defs}"
    OUTPUT_DIR      "${_xr_config_out}"
    DEFINITIONS
        "${_xr_config_defs}/headset_config.py"
        "${_xr_config_defs}/perf_settings_level.py"
)

set(_xr_config_hpps
    ${_xr_config_out}/headset_config.hpp
    ${_xr_config_out}/perf_settings_level.hpp
)
set_source_files_properties(${_xr_config_hpps} PROPERTIES GENERATED TRUE)
target_sources(${_target} PRIVATE ${_xr_config_hpps})
source_group("codegen generated" FILES ${_xr_config_hpps})

# TODO Use pimpl in Xr_instance to be able to replace PUBLIC with PRIVATE
if (ERHE_TARGET_OS_WINDOWS)
    target_compile_definitions(
        ${_target}
        PUBLIC
        XR_USE_PLATFORM_WIN32=1
    )
endif ()

if (ERHE_TARGET_OS_LINUX)
    target_compile_definitions(
        ${_target}
        PUBLIC
        XR_USE_PLATFORM_LINUX=1
        XR_USE_PLATFORM_WAYLAND=1
    )
endif ()

if (ERHE_TARGET_OS_ANDROID)
    target_compile_definitions(
        ${_target}
        PUBLIC
        XR_USE_PLATFORM_ANDROID=1
    )
endif ()

# TODO ERHE_TARGET_OS_OSX?

if (ERHE_GRAPHICS_API_OPENGL)
    # TODO; Since OpenXR gives native OpenGL format enum
    target_link_libraries(${_target} PRIVATE erhe::gl)
    target_compile_definitions(${_target} PUBLIC XR_USE_GRAPHICS_API_OPENGL=1)
endif ()

# TODO Try to move volk into PRIVATE by using pimpl
if (ERHE_GRAPHICS_API_VULKAN)
    target_link_libraries(
        ${_target}
        PUBLIC
            volk::volk
        PRIVATE
            GPUOpen::VulkanMemoryAllocator
    )
    target_compile_definitions(${_target} PUBLIC XR_USE_GRAPHICS_API_VULKAN=1)
endif ()

target_compile_definitions(${_target} PRIVATE XR_API_LAYER_PATH="${OPENXR_BINARY_DIR}/src/api_layers")

# erhe::graphics is a PUBLIC dependency because Xr_session::m_graphics_device
# is an erhe::graphics::Device& member declared in the public header, and
# xr_session.cpp uses Device::probe_image_format_support / Image_usage_flag_bit_mask
# in both the GL and Vulkan branches.
target_link_libraries(${_target}
    PUBLIC
        erhe::codegen
        erhe::dataformat
        erhe::graphics
        erhe::window
        glm::glm-header-only
    PRIVATE
        erhe::log
        erhe::profile
        erhe::utility
        erhe::verify
        etl::etl
        fmt::fmt
)

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE openxr_loader)
endif ()

erhe_target_settings(${_target} "erhe")
