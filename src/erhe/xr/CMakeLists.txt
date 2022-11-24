set(_target "erhe_xr")
add_library(${_target})
add_library(erhe::xr ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    headset.cpp
    headset.hpp
    xr_instance.cpp
    xr_instance.hpp
    xr_log.cpp
    xr_log.hpp
    xr_session.cpp
    xr_session.hpp
    xr_swapchain_image.cpp
    xr_swapchain_image.hpp
    xr.cpp
    xr.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        erhe::gl
        erhe::toolkit
        glm::glm
    PRIVATE
        erhe::log
        fmt::fmt
        Microsoft.GSL::GSL
        openxr_loader
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
