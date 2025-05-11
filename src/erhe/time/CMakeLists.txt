set(_target "erhe_time")
add_library(${_target})
add_library(erhe::time ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_time/sleep.cpp
    erhe_time/sleep.hpp
    erhe_time/time_log.cpp
    erhe_time/time_log.hpp
    erhe_time/timer.cpp
    erhe_time/timer.hpp
    erhe_time/timestamp.cpp
    erhe_time/timestamp.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        fmt::fmt
    PRIVATE
        erhe::log
        erhe::profile
)

erhe_target_settings(${_target} "erhe")
