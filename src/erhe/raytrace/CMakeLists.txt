set(_target "erhe_raytrace")

if (${ERHE_RAYTRACE_LIBRARY} STREQUAL "embree")
    message("erhe_raytrace using embree backend")
    set(impl_source_list
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
    message("erhe_raytrace using null backend")
    set(impl_source_list
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

set(source_list
    ibuffer.hpp
    igeometry.hpp
    iinstance.hpp
    iscene.hpp
    log.cpp
    log.hpp
    mesh_intersect.cpp
    mesh_intersect.hpp
)

add_library(${_target} ${source_list} ${impl_source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

message("impl_link_libraries = ${impl_link_libraries}")
target_link_libraries(${_target}
    PRIVATE
        ${impl_link_libraries}
        erhe_log
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
    PUBLIC
        erhe_primitive
        erhe_scene
)

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

target_strict_cxx(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
