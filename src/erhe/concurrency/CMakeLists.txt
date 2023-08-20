set(_target "erhe_concurrency")
add_library(${_target})
add_library(erhe::concurrency ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_concurrency/thread_pool.cpp
    erhe_concurrency/thread_pool.hpp
    erhe_concurrency/concurrent_queue.cpp
    erhe_concurrency/concurrent_queue.hpp
    erhe_concurrency/serial_queue.cpp
    erhe_concurrency/serial_queue.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${_target} PUBLIC concurrentqueue)
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
