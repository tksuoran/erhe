set(source_list
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

add_library(erhe_primitive ${source_list})

add_library(erhe::primitive ALIAS erhe_primitive)

target_include_directories(erhe_primitive
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(erhe_primitive
    PUBLIC
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        TracyClient::TracyClient
        erhe::gl
        erhe::log
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
