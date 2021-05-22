set(_target "erhe_primitive")

set(source_list
    enums.hpp
    format_info.hpp
    buffer_info.hpp
    primitive_builder.cpp
    primitive_builder.hpp
    index_range.cpp
    index_range.hpp
    log.cpp
    log.hpp
    material.cpp
    material.hpp
    primitive.cpp
    primitive.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        erhe_geometry
        erhe_gl
        erhe_log
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
