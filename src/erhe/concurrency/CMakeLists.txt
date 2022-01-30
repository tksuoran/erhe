set(_target "erhe_concurrency")

add_library(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    thread_pool.cpp
    thread_pool.hpp
    concurrent_queue.cpp
    concurrent_queue.hpp
    serial_queue.cpp
    serial_queue.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target} PUBLIC concurrentqueue)

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
