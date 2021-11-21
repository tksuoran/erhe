set(_target "erhe_xr")

set(source_list
    headset.cpp
    headset.hpp
    xr_instance.cpp
    xr_instance.hpp
    xr_session.cpp
    xr_session.hpp
    xr_swapchain_image.cpp
    xr_swapchain_image.hpp
    xr.cpp
    xr.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        erhe_gl
        erhe_toolkit
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        openxr_loader
)

target_strict_cxx(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
