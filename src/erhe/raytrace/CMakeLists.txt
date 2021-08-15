set(_target "erhe_raytrace")

set(source_list
    buffer.cpp
    buffer.hpp
    bvh.cpp
    bvh.hpp
    device.cpp
    device.hpp
    geometry.cpp
    geometry.hpp
    log.cpp
    log.hpp
    ray.hpp
    rayhit.hpp
    scene.cpp
    scene.hpp
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        embree
        erhe_log
        erhe_primitive
        erhe_scene
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()