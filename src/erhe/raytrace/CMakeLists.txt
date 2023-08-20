set(_target "erhe_raytrace")
add_library(${_target})
add_library(erhe::raytrace ALIAS ${_target})
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_raytrace/embree/embree_buffer.cpp
        erhe_raytrace/embree/embree_buffer.hpp
        erhe_raytrace/embree/embree_device.cpp
        erhe_raytrace/embree/embree_device.hpp
        erhe_raytrace/embree/embree_geometry.cpp
        erhe_raytrace/embree/embree_geometry.hpp
        erhe_raytrace/embree/embree_instance.cpp
        erhe_raytrace/embree/embree_instance.hpp
        erhe_raytrace/embree/embree_scene.cpp
        erhe_raytrace/embree/embree_scene.hpp
    )
    set(impl_link_libraries embree)
endif ()
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "bvh")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_raytrace/bvh/bvh_buffer.cpp
        erhe_raytrace/bvh/bvh_buffer.hpp
        erhe_raytrace/bvh/bvh_geometry.cpp
        erhe_raytrace/bvh/bvh_geometry.hpp
        erhe_raytrace/bvh/bvh_instance.cpp
        erhe_raytrace/bvh/bvh_instance.hpp
        erhe_raytrace/bvh/bvh_scene.cpp
        erhe_raytrace/bvh/bvh_scene.hpp
    )
    set(impl_link_libraries bvh)
endif ()
if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "none")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_raytrace/null/null_buffer.cpp
        erhe_raytrace/null/null_buffer.hpp
        erhe_raytrace/null/null_geometry.cpp
        erhe_raytrace/null/null_geometry.hpp
        erhe_raytrace/null/null_instance.cpp
        erhe_raytrace/null/null_instance.hpp
        erhe_raytrace/null/null_scene.cpp
        erhe_raytrace/null/null_scene.hpp
    )
endif ()
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_raytrace/ibuffer.hpp
    erhe_raytrace/igeometry.hpp
    erhe_raytrace/iinstance.hpp
    erhe_raytrace/iscene.hpp
    erhe_raytrace/mesh_intersect.cpp
    erhe_raytrace/mesh_intersect.hpp
    erhe_raytrace/ray.cpp
    erhe_raytrace/ray.hpp
    erhe_raytrace/raytrace_log.cpp
    erhe_raytrace/raytrace_log.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        erhe::hash
        erhe::primitive
        erhe::profile
        erhe::scene
        erhe::verify
    PRIVATE
        ${impl_link_libraries}
        erhe::log
        erhe::time
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)
if (DEFINED ERHE_RAYTRACE_TARGET)
    target_link_libraries(${_target} PUBLIC ${ERHE_RAYTRACE_TARGET})
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
