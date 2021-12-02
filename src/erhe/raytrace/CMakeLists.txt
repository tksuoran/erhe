set(_target "erhe_raytrace")

set(source_list
    #embree/embree_buffer.cpp
    #embree/embree_buffer.hpp
    #embree/embree_geometry.cpp
    #embree/embree_geometry.hpp
    #embree/embree_scene.cpp
    #embree/embree_scene.hpp
    null/null_buffer.cpp
    null/null_buffer.hpp
    null/null_geometry.cpp
    null/null_geometry.hpp
    null/null_scene.cpp
    null/null_scene.hpp
    #buffer.cpp
    #buffer.hpp
    ibuffer.hpp
    igeometry.hpp
    iscene.hpp
    log.cpp
    log.hpp
    mesh_intersect.cpp
    mesh_intersect.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PRIVATE
        #embree
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
