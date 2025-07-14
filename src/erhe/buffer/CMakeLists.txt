set(_target "erhe_buffer")
add_library(${_target})
add_library(erhe::buffer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_buffer/ibuffer.hpp
    erhe_buffer/ibuffer.cpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        erhe::utility
    PRIVATE
        erhe::profile
        erhe::verify
)

erhe_target_settings(${_target} "erhe")
