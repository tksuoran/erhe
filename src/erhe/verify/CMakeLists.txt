set(_target "erhe_verify")
add_library(${_target})
add_library(erhe::verify ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_verify/verify.cpp
    erhe_verify/verify.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(${_target} PUBLIC cpptrace::cpptrace)

if (ERHE_TARGET_OS_ANDROID)
    # verify.cpp writes fatal reports to logcat; verify.hpp includes
    # <android/log.h>, so liblog propagates to consumers.
    target_link_libraries(${_target} PUBLIC log)
endif ()

erhe_target_settings(${_target} "erhe")
