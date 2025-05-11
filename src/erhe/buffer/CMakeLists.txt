set(_target "erhe_buffer")
add_library(${_target})
add_library(erhe::buffer ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_buffer/ibuffer.hpp
    erhe_buffer/ibuffer.cpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
    target_link_libraries(${_target} PUBLIC erhe_pch)
endif ()

erhe_target_settings(${_target})
target_link_libraries(${_target}
    PRIVATE
        erhe::profile
        erhe::verify
)

set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
