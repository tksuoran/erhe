set(_target "erhe_primitive")
add_library(${_target})
add_library(erhe::primitive ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    buffer_info.hpp
    buffer_range.cpp
    buffer_range.hpp
    buffer_sink.cpp
    buffer_sink.hpp
    buffer_writer.cpp
    buffer_writer.hpp
    build_info.hpp
    enums.hpp
    format_info.cpp
    format_info.hpp
    index_range.cpp
    index_range.hpp
    material.cpp
    material.hpp
    primitive.cpp
    primitive.hpp
    primitive_builder.cpp
    primitive_builder.hpp
    primitive_geometry.hpp
    primitive_geometry.cpp
    primitive_log.cpp
    primitive_log.hpp
    property_maps.hpp
    property_maps.cpp
    vertex_attribute_info.hpp
    vertex_attribute_info.cpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PRIVATE
        erhe::log
        fmt::fmt
        MathGeoLib
        Microsoft.GSL::GSL
    PUBLIC
        glm::glm
        etl::etl
        erhe::geometry
        erhe::gl
        erhe::graphics # TODO Get rid of this dependency?
        erhe::raytrace
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

if (DEFINED ERHE_RAYTRACE_TARGET)
    target_link_libraries(${_target} PUBLIC ${ERHE_RAYTRACE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
