set(_target "erhe_raytrace")
add_library(${_target})
add_library(erhe::raytrace ALIAS ${_target})

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        embree/embree_buffer.cpp
        embree/embree_buffer.hpp
        embree/embree_device.cpp
        embree/embree_device.hpp
        embree/embree_geometry.cpp
        embree/embree_geometry.hpp
        embree/embree_instance.cpp
        embree/embree_instance.hpp
        embree/embree_scene.cpp
        embree/embree_scene.hpp
    )
    set(impl_link_libraries embree)
endif ()

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "none")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        null/null_buffer.cpp
        null/null_buffer.hpp
        null/null_geometry.cpp
        null/null_geometry.hpp
        null/null_instance.cpp
        null/null_instance.hpp
        null/null_scene.cpp
        null/null_scene.hpp
    )
endif ()

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ibuffer.hpp
    igeometry.hpp
    iinstance.hpp
    iscene.hpp
    log.cpp
    log.hpp
    mesh_intersect.cpp
    mesh_intersect.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PRIVATE
        ${impl_link_libraries}
        erhe::log
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
    PUBLIC
        erhe::primitive
        erhe::scene
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
