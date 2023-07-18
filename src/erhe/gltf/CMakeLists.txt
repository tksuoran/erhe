set(_target erhe_gltf)
add_library(${_target})
add_library(erhe::gltf ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    gltf.cpp
    gltf.hpp
    gltf_log.cpp
    gltf_log.hpp
    image_transfer.cpp
    image_transfer.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        cgltf
        fmt::fmt
        etl::etl
        erhe::geometry
        erhe::graphics
        erhe::log
        erhe::primitive
        erhe::scene
        erhe::toolkit
        mINI
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
