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
        erhe_xr/xr_session.cpp
        erhe_xr/xr_session.hpp
        erhe_xr/xr_swapchain_image.cpp
        erhe_xr/xr_swapchain_image.hpp
        erhe_xr/xr.cpp
        erhe_xr/xr.hpp
    )
endif ()

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

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

# TODO ERHE_TARGET_OS_OSX?

if (ERHE_GRAPHICS_API_OPENGL)
    # TODO; Since OpenXR gives native OpenGL format enum
    target_link_libraries(${_target} PRIVATE erhe::gl)
    target_compile_definitions(${_target} PUBLIC XR_USE_GRAPHICS_API_OPENGL=1)
endif ()

if (ERHE_GRAPHICS_API_VULKAN)
    target_compile_definitions(${_target} PUBLIC XR_USE_GRAPHICS_API_VULKAN=1)
endif ()

target_compile_definitions(${_target} PRIVATE XR_API_LAYER_PATH="${OPENXR_BINARY_DIR}/src/api_layers")

target_link_libraries(${_target}
    PUBLIC
        erhe::dataformat
        erhe::window
        glm::glm-header-only
    PRIVATE
        erhe::log
        erhe::profile
        erhe::verify
        etl::etl
        fmt::fmt
)

if (${ERHE_XR_LIBRARY} STREQUAL "openxr")
    target_link_libraries(${_target} PRIVATE openxr_loader)
endif ()

erhe_target_settings(${_target} "erhe")
