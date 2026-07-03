set(_target "erhe_texgen")
add_library(${_target})
add_library(erhe::texgen ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_texgen/common_library.cpp
    erhe_texgen/common_library.hpp
    erhe_texgen/compose_node.cpp
    erhe_texgen/compose_node.hpp
    erhe_texgen/composer.cpp
    erhe_texgen/composer.hpp
    erhe_texgen/node_descriptor.cpp
    erhe_texgen/node_descriptor.hpp
    erhe_texgen/shader_code.cpp
    erhe_texgen/shader_code.hpp
    erhe_texgen/value_type.cpp
    erhe_texgen/value_type.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PRIVATE
        fmt::fmt
        erhe::verify
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
