set(_target erhe_gltf)
add_library(${_target})
add_library(erhe::gltf ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_gltf/gltf_log.cpp
    erhe_gltf/gltf_log.hpp
    erhe_gltf/image_transfer.cpp
    erhe_gltf/image_transfer.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_GLTF_LIBRARY} STREQUAL "fastgltf")
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_gltf/gltf_fastgltf.cpp
        erhe_gltf/gltf_fastgltf.hpp
    )
    target_link_libraries(${_target} PRIVATE fastgltf)
elseif ()
    erhe_target_sources_grouped(
        ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
        erhe_gltf/gltf_none.cpp
        erhe_gltf/gltf_none.hpp
    )
    target_link_libraries(${_target} PRIVATE fastgltf)
endif ()

target_link_libraries(${_target}
    PRIVATE
        fmt::fmt
        erhe::file
        erhe::profile
        erhe::geometry
        erhe::graphics
        erhe::log
        erhe::primitive
        erhe::scene
)

erhe_target_settings(${_target} "erhe")
