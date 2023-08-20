set(_target "erhe_geometry")
add_library(${_target})
add_library(erhe::geometry ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_geometry/corner.inl
    erhe_geometry/geometry.cpp
    erhe_geometry/geometry.hpp
    erhe_geometry/geometry.inl
    erhe_geometry/geometry_iterators.cpp
    erhe_geometry/geometry_log.cpp
    erhe_geometry/geometry_log.hpp
    erhe_geometry/geometry_make.cpp
    erhe_geometry/geometry_merge.cpp
    erhe_geometry/geometry_tangents.cpp
    erhe_geometry/operation/ambo.cpp
    erhe_geometry/operation/ambo.hpp
    erhe_geometry/operation/catmull_clark_subdivision.cpp
    erhe_geometry/operation/catmull_clark_subdivision.hpp
    erhe_geometry/operation/clone.cpp
    erhe_geometry/operation/clone.hpp
    erhe_geometry/operation/dual.cpp
    erhe_geometry/operation/dual.hpp
    erhe_geometry/operation/geometry_operation.cpp
    erhe_geometry/operation/geometry_operation.hpp
    erhe_geometry/operation/gyro.cpp
    erhe_geometry/operation/gyro.hpp
    erhe_geometry/operation/join.cpp
    erhe_geometry/operation/join.hpp
    erhe_geometry/operation/kis.cpp
    erhe_geometry/operation/kis.hpp
    erhe_geometry/operation/meta.cpp
    erhe_geometry/operation/meta.hpp
    erhe_geometry/operation/normalize.cpp
    erhe_geometry/operation/normalize.hpp
    erhe_geometry/operation/reverse.cpp
    erhe_geometry/operation/reverse.hpp
    erhe_geometry/operation/sqrt3_subdivision.cpp
    erhe_geometry/operation/sqrt3_subdivision.hpp
    erhe_geometry/operation/subdivide.cpp
    erhe_geometry/operation/subdivide.hpp
    erhe_geometry/operation/triangulate.cpp
    erhe_geometry/operation/triangulate.hpp
    erhe_geometry/operation/truncate.cpp
    erhe_geometry/operation/truncate.hpp
    erhe_geometry/operation/weld.cpp
    erhe_geometry/operation/weld.hpp
    erhe_geometry/polygon.cpp
    erhe_geometry/polygon.inl
    erhe_geometry/property_map.hpp
    erhe_geometry/property_map.inl
    erhe_geometry/property_map_collection.hpp
    erhe_geometry/property_map_collection.inl
    erhe_geometry/remapper.hpp
    erhe_geometry/shapes/box.cpp
    erhe_geometry/shapes/box.hpp
    erhe_geometry/shapes/cone.cpp
    erhe_geometry/shapes/cone.hpp
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
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
        glm::glm
    PRIVATE
        erhe::log
        erhe::math
        erhe::profile
        erhe::verify
        Microsoft.GSL::GSL
        mikktspace
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
