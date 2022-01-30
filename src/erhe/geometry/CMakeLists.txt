set(_target "erhe_geometry")

add_library(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    corner.inl
    geometry.cpp
    geometry_iterators.cpp
    geometry_make.cpp
    geometry_merge.cpp
    geometry_tangents.cpp
    geometry_weld.cpp
    geometry.hpp
    geometry.inl
    log.cpp
    log.hpp
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
    polygon.cpp
    polygon.inl
    property_map_collection.hpp
    property_map_collection.inl
    property_map.hpp
    property_map.inl
    shapes/box.cpp
    shapes/box.hpp
    shapes/cone.cpp
    shapes/cone.hpp
    shapes/disc.cpp
    shapes/disc.hpp
    shapes/sphere.cpp
    shapes/sphere.hpp
    shapes/torus.cpp
    shapes/torus.hpp
    shapes/regular_polygon.cpp
    shapes/regular_polygon.hpp
    shapes/regular_polyhedron.cpp
    shapes/regular_polyhedron.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

#target_precompile_headers(${_target} PRIVATE precompiled.hpp)
#target_precompile_headers(${_target} PRIVATE geometry.hpp)

target_link_libraries(${_target}
    PUBLIC
        erhe_log
        erhe_toolkit
        fmt::fmt
        glm::glm
        #gtmathematics
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
