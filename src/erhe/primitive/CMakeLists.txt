set(_target "erhe_primitive")

set(source_list
    buffer_info.hpp
    buffer_range.cpp
    buffer_range.hpp
    buffer_sink.cpp
    buffer_sink.hpp
    buffer_writer.cpp
    buffer_writer.hpp
    build_info.hpp
    enums.hpp
    format_info.hpp
    index_range.cpp
    index_range.hpp
    log.cpp
    log.hpp
    material.cpp
    material.hpp
    primitive.cpp
    primitive.hpp
    primitive_builder.cpp
    primitive_builder.hpp
    primitive_geometry.hpp
    primitive_geometry.cpp
    property_maps.hpp
    property_maps.cpp
    vertex_attribute_info.hpp
    vertex_attribute_info.cpp
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
        erhe_raytrace
)

target_strict_cxx(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    target_link_libraries(${_target} PUBLIC embree)
endif ()

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
