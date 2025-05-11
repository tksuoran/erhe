set(_target "erhe_verify")
add_library(${_target})
add_library(erhe::verify ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_verify/verify.cpp
    erhe_verify/verify.hpp
)

target_include_directories(${_target} INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

erhe_target_settings(${_target} "erhe")
