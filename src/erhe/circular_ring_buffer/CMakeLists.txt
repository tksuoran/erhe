set(_target "erhe_circular_ring_buffer")
add_library(${_target})
add_library(erhe::circular_ring_buffer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_circular_ring_buffer/circular_ring_buffer_algorithm.cpp
    erhe_circular_ring_buffer/circular_ring_buffer_algorithm.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PRIVATE
        erhe::utility
        erhe::verify
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
