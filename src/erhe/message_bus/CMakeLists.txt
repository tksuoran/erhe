set(_target "erhe_message_bus")
add_library(${_target})
add_library(erhe::message_bus ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_message_bus/message_bus.cpp
    erhe_message_bus/message_bus.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
    target_link_libraries(${_target} PUBLIC erhe_pch)
endif ()

target_link_libraries(${_target} PUBLIC erhe::profile)
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
