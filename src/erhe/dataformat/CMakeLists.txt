set(_target "erhe_dataformat")
add_library(${_target})
add_library(erhe::dataformat ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_dataformat/dataformat.cpp
    erhe_dataformat/dataformat.hpp
    erhe_dataformat/dataformat_log.cpp
    erhe_dataformat/dataformat_log.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
target_link_libraries(${_target}
    PUBLIC
        erhe::log
        erhe::verify
        fmt::fmt
)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
