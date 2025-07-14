set(_target "erhe_utility")
add_library(${_target})
add_library(erhe::utility ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_utility/bit_helpers.cpp
    erhe_utility/bit_helpers.hpp
    erhe_utility/align.cpp
    erhe_utility/align.hpp
)

target_include_directories(${_target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

erhe_target_settings(${_target} "erhe")
