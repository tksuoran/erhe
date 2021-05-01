set(source_list
    log.cpp
    log.hpp
)

add_library(erhe_log ${source_list})

add_library(erhe::log ALIAS erhe_log)

target_include_directories(erhe_log
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(erhe_log
    PUBLIC
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
