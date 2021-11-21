set(_target "erhe_components")

set(source_list
    log.cpp
    log.hpp
    component.cpp
    component.hpp
    components.cpp
    components.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        erhe_log
        mango
        Microsoft.GSL::GSL
        TracyClient
)

target_strict_cxx(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
