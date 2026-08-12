set(_target "erhe_voxel")
add_library(${_target})
add_library(erhe::voxel ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_voxel/voxel.cpp
    erhe_voxel/voxel.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        erhe::geometry
        erhe::math
        glm::glm-header-only
    PRIVATE
        openvdb
        erhe::verify
)

if (MSVC)
    # OpenVDB's template-heavy headers overflow the default COFF section limit.
    target_compile_options(${_target} PRIVATE /bigobj)
endif ()

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
