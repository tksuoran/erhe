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
    erhe_primitive/enums.hpp
    erhe_primitive/format_info.cpp
    erhe_primitive/format_info.hpp
    erhe_primitive/index_range.cpp
    erhe_primitive/index_range.hpp
    erhe_primitive/material.cpp
    erhe_primitive/material.hpp
    erhe_primitive/primitive_builder.cpp
    erhe_primitive/primitive_builder.hpp
    erhe_primitive/primitive_log.cpp
    erhe_primitive/primitive_log.hpp
    erhe_primitive/primitive.cpp
    erhe_primitive/primitive.hpp
    erhe_primitive/triangle_soup.cpp
    erhe_primitive/triangle_soup.hpp
    erhe_primitive/vertex_attribute_info.cpp
    erhe_primitive/vertex_attribute_info.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        geogram
        glm::glm-header-only
        etl::etl
        erhe::buffer
        erhe::dataformat
        erhe::geometry
        erhe::item
        erhe::math
        erhe::raytrace
    PRIVATE
        erhe::log
        erhe::profile
        erhe::verify
        fmt::fmt
        MathGeoLib
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
