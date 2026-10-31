set(_target "erhe_primitive")
add_library(${_target})
add_library(erhe::primitive ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_primitive/buffer_info.cpp
    erhe_primitive/buffer_info.hpp
    erhe_primitive/buffer_mesh.cpp
    erhe_primitive/buffer_mesh.hpp
    erhe_primitive/buffer_range.cpp
    erhe_primitive/buffer_range.hpp
    erhe_primitive/buffer_sink.cpp
    erhe_primitive/buffer_sink.hpp
    erhe_primitive/buffer_writer.cpp
    erhe_primitive/buffer_writer.hpp
    erhe_primitive/build_info.cpp
    erhe_primitive/build_info.hpp
    erhe_primitive/edge_line_vertex.cpp
    erhe_primitive/edge_line_vertex.hpp
    erhe_primitive/enums.hpp
    erhe_primitive/format_info.cpp
    erhe_primitive/format_info.hpp
    erhe_primitive/index_range.cpp
    erhe_primitive/index_range.hpp
    erhe_primitive/lod.cpp
    erhe_primitive/lod.hpp
    erhe_primitive/material.cpp
    erhe_primitive/material.hpp
    erhe_primitive/mesh_optimizer.cpp
    erhe_primitive/mesh_optimizer.hpp
    erhe_primitive/mesh_optimizer_cache.cpp
    erhe_primitive/prepared_primitive.cpp
    erhe_primitive/prepared_primitive.hpp
    erhe_primitive/primitive_builder.cpp
    erhe_primitive/primitive_builder.hpp
    erhe_primitive/primitive_log.cpp
    erhe_primitive/primitive_log.hpp
    erhe_primitive/primitive.cpp
    erhe_primitive/primitive.hpp
    erhe_primitive/triangle_soup.cpp
    erhe_primitive/stored_vertex_streams.hpp
    erhe_primitive/triangle_soup.hpp
    erhe_primitive/vertex_attribute_info.cpp
    erhe_primitive/vertex_attribute_info.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        geogram
        glm::glm-header-only
        erhe::buffer
        erhe::dataformat
        erhe::geometry
        erhe::item
        erhe::math
        erhe::raytrace
        # Material_sampler_state names the erhe::graphics sampler enums in
        # the public header.
        erhe::graphics
    PRIVATE
        erhe::file
        erhe::hash
        erhe::log
        erhe::profile
        erhe::verify
        fmt::fmt
        meshoptimizer
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
