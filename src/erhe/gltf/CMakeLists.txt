set(_target erhe_gltf)
add_library(${_target})
add_library(erhe::gltf ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_gltf/gltf.cpp
    erhe_gltf/gltf.hpp
    erhe_gltf/gltf_log.cpp
    erhe_gltf/gltf_log.hpp
    erhe_gltf/image_transfer.cpp
    erhe_gltf/image_transfer.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PRIVATE
        cgltf
        fmt::fmt
        erhe::file
        erhe::profile
        erhe::geometry
        #erhe::graphics
        IGLLibrary
        erhe::log
        erhe::primitive
        erhe::scene
)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
