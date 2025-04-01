set(_target "tinyexpr")
add_library(${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    tinyexpr/tinyexpr.c
    tinyexpr/tinyexpr.h
)
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tinyexpr)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
