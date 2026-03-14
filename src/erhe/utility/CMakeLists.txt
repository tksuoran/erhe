set(_target "erhe_utility")
add_library(${_target})
add_library(erhe::utility ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_utility/align.cpp
    erhe_utility/align.hpp
    erhe_utility/bit_helpers.cpp
    erhe_utility/bit_helpers.hpp
    erhe_utility/debug_label.cpp
    erhe_utility/debug_label.hpp
    erhe_utility/pimpl_ptr.cpp
    erhe_utility/pimpl_ptr.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

erhe_target_settings(${_target} "erhe")
