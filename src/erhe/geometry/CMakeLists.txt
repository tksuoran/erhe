set(_target "erhe_geometry")

set(source_list
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
    mikktspace.c
    mikktspace.h
    operation/catmull_clark_subdivision.cpp
    operation/catmull_clark_subdivision.hpp
    operation/ambo.hpp
    operation/ambo.cpp
    operation/dual.hpp
    operation/dual.cpp
    operation/truncate.hpp
    operation/truncate.cpp
    operation/sqrt3_subdivision.cpp
    operation/sqrt3_subdivision.hpp
    operation/clone.cpp
    operation/clone.hpp
    operation/geometry_operation.cpp
    operation/geometry_operation.hpp
    operation/subdivide.cpp
    operation/subdivide.hpp
    operation/triangulate.cpp
    operation/triangulate.hpp
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

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

#target_precompile_headers(${_target} PRIVATE precompiled.hpp)
#target_precompile_headers(${_target} PRIVATE geometry.hpp)

target_link_libraries(${_target}
    PUBLIC
        erhe_log
        erhe_toolkit
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        TracyClient
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
