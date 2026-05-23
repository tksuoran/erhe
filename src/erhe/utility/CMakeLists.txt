set(_target "erhe_utility")
add_library(${_target})
add_library(erhe::utility ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_utility/align.cpp
    erhe_utility/align.hpp
    erhe_utility/bit_helpers.cpp
    erhe_utility/bit_helpers.hpp
    erhe_utility/clipboard.cpp
    erhe_utility/clipboard.hpp
    erhe_utility/debug_label.cpp
    erhe_utility/debug_label.hpp
    erhe_utility/env.cpp
    erhe_utility/env.hpp
    erhe_utility/pimpl_ptr.cpp
    erhe_utility/pimpl_ptr.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (ERHE_TARGET_OS_ANDROID)
    target_link_libraries(${_target} PRIVATE log)
else ()
    target_link_libraries(${_target} PRIVATE SDL3::SDL3-static)
endif ()

erhe_target_settings(${_target} "erhe")
