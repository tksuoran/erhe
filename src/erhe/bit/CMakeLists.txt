set(_target "erhe_bit")
add_library(${_target})
add_library(erhe::bit ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_bit/bit_helpers.cpp
    erhe_bit/bit_helpers.hpp
)

target_include_directories(${_target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
