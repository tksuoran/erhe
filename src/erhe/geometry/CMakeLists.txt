set(_target "erhe_geometry")
add_library(${_target})
add_library(erhe::geometry ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_geometry/geometry.cpp
    erhe_geometry/geometry.hpp
    erhe_geometry/geometry_log.cpp
    erhe_geometry/geometry_log.hpp
    erhe_geometry/geometry_tangents.cpp

    erhe_geometry/operation/csg/difference.cpp
    erhe_geometry/operation/csg/difference.hpp
    erhe_geometry/operation/csg/intersection.cpp
    erhe_geometry/operation/csg/intersection.hpp
    erhe_geometry/operation/csg/union.cpp
    erhe_geometry/operation/csg/union.hpp

    erhe_geometry/operation/conway/ambo.cpp
    erhe_geometry/operation/conway/ambo.hpp
    erhe_geometry/operation/conway/chamfer.cpp
    erhe_geometry/operation/conway/chamfer.hpp
    erhe_geometry/operation/conway/dual.cpp
    erhe_geometry/operation/conway/dual.hpp
    erhe_geometry/operation/conway/gyro.cpp
    erhe_geometry/operation/conway/gyro.hpp
    erhe_geometry/operation/conway/join.cpp
    erhe_geometry/operation/conway/join.hpp
    erhe_geometry/operation/conway/kis.cpp
    erhe_geometry/operation/conway/kis.hpp
    erhe_geometry/operation/conway/meta.cpp
    erhe_geometry/operation/conway/meta.hpp
    erhe_geometry/operation/conway/truncate.cpp
    erhe_geometry/operation/conway/truncate.hpp

    erhe_geometry/operation/subdivision/catmull_clark_subdivision.cpp
    erhe_geometry/operation/subdivision/catmull_clark_subdivision.hpp
    erhe_geometry/operation/subdivision/sqrt3_subdivision.cpp
    erhe_geometry/operation/subdivision/sqrt3_subdivision.hpp

    erhe_geometry/operation/bake_transform.cpp
    erhe_geometry/operation/bake_transform.hpp
    erhe_geometry/operation/generate_tangents.cpp
    erhe_geometry/operation/generate_tangents.hpp
    erhe_geometry/operation/geometry_operation.cpp
    erhe_geometry/operation/geometry_operation.hpp
    erhe_geometry/operation/normalize.cpp
    erhe_geometry/operation/normalize.hpp
    erhe_geometry/operation/repair.cpp
    erhe_geometry/operation/repair.hpp
    erhe_geometry/operation/reverse.cpp
    erhe_geometry/operation/reverse.hpp
    erhe_geometry/operation/subdivide.cpp
    erhe_geometry/operation/subdivide.hpp
    erhe_geometry/operation/triangulate.cpp
    erhe_geometry/operation/triangulate.hpp

    erhe_geometry/shapes/box.cpp
    erhe_geometry/shapes/box.hpp
    erhe_geometry/shapes/cone.cpp
    erhe_geometry/shapes/cone.hpp
    erhe_geometry/shapes/convex_hull.cpp
    erhe_geometry/shapes/convex_hull.hpp
    erhe_geometry/shapes/disc.cpp
    erhe_geometry/shapes/disc.hpp
    erhe_geometry/shapes/regular_polygon.cpp
    erhe_geometry/shapes/regular_polygon.hpp
    erhe_geometry/shapes/regular_polyhedron.cpp
    erhe_geometry/shapes/regular_polyhedron.hpp
    erhe_geometry/shapes/sphere.cpp
    erhe_geometry/shapes/sphere.hpp
    erhe_geometry/shapes/torus.cpp
    erhe_geometry/shapes/torus.hpp
    erhe_geometry/types.hpp
    mikktspace/mikktspace.cpp
    mikktspace/mikktspace.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        geogram
        glm::glm-header-only
    PRIVATE
        erhe::log
        erhe::math
        erhe::profile
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
