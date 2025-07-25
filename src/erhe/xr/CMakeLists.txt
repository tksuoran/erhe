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

target_link_libraries(${_target}
    PUBLIC
        erhe::gl # TODO; Since OpenXR gives native OpenGL format enum
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
