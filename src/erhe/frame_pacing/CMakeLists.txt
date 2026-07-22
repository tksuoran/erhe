set(_target "erhe_frame_pacing")
add_library(${_target})
add_library(erhe::frame_pacing ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_frame_pacing/frame_pacer.cpp
    erhe_frame_pacing/frame_pacer.hpp
    erhe_frame_pacing/frame_pacing_observer.cpp
    erhe_frame_pacing/frame_pacing_observer.hpp
    erhe_frame_pacing/frame_time_recorder.cpp
    erhe_frame_pacing/frame_time_recorder.hpp
    erhe_frame_pacing/slop_servo_pacer.cpp
    erhe_frame_pacing/slop_servo_pacer.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
