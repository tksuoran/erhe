set(source_list
    log.cpp
    log.hpp
    component.cpp
    component.hpp
    components.cpp
    components.hpp
)

add_library(erhe_components ${source_list})

add_library(erhe::components ALIAS erhe_components)

target_include_directories(erhe_components
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(erhe_components
    PUBLIC
        fmt::fmt
        erhe::log
        Microsoft.GSL::GSL
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
