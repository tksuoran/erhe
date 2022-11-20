set(_target "erhe_geometry")
add_library(${_target})
add_library(erhe::geometry ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    corner.inl
    geometry.cpp
    geometry.hpp
    geometry.inl
    geometry_iterators.cpp
    geometry_log.cpp
    geometry_log.hpp
    geometry_make.cpp
    geometry_merge.cpp
    geometry_tangents.cpp
    operation/ambo.cpp
    operation/ambo.hpp
    operation/catmull_clark_subdivision.cpp
    operation/catmull_clark_subdivision.hpp
    operation/clone.cpp
    operation/clone.hpp
    operation/dual.cpp
    operation/dual.hpp
    operation/geometry_operation.cpp
    operation/geometry_operation.hpp
    operation/gyro.cpp
    operation/gyro.hpp
    operation/join.cpp
    operation/join.hpp
    operation/kis.cpp
    operation/kis.hpp
    operation/meta.cpp
    operation/meta.hpp
    operation/normalize.cpp
    operation/normalize.hpp
    operation/reverse.cpp
    operation/reverse.hpp
    operation/sqrt3_subdivision.cpp
    operation/sqrt3_subdivision.hpp
    operation/subdivide.cpp
    operation/subdivide.hpp
    operation/triangulate.cpp
    operation/triangulate.hpp
    operation/truncate.cpp
    operation/truncate.hpp
    operation/weld.cpp
    operation/weld.hpp
    polygon.cpp
    polygon.inl
    property_map.hpp
    property_map.inl
    property_map_collection.hpp
    property_map_collection.inl
    remapper.hpp
    shapes/box.cpp
    shapes/box.hpp
    shapes/cone.cpp
    shapes/cone.hpp
    shapes/disc.cpp
    shapes/disc.hpp
    shapes/regular_polygon.cpp
    shapes/regular_polygon.hpp
    shapes/regular_polyhedron.cpp
    shapes/regular_polyhedron.hpp
    shapes/sphere.cpp
    shapes/sphere.hpp
    shapes/torus.cpp
    shapes/torus.hpp
    types.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

#target_precompile_headers(${_target} PRIVATE precompiled.hpp)
#target_precompile_headers(${_target} PRIVATE geometry.hpp)

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        glm::glm
        #gtmathematics
    PRIVATE
        erhe::log
        erhe::toolkit
        optional_lite
        Microsoft.GSL::GSL
        mikktspace
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
