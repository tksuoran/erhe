set(_target "erhe_task")
add_library(${_target})
add_library(erhe::task ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_task/executor.cpp
    erhe_task/executor.hpp
    erhe_task/task.cpp
    erhe_task/task.hpp
    erhe_task/task_log.cpp
    erhe_task/task_log.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PUBLIC
        Taskflow
        spdlog::spdlog_header_only
    PRIVATE
        erhe::graphics
        erhe::log
        erhe::profile
        erhe::verify
        fmt::fmt
)

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
