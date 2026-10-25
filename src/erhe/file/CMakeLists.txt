set(_target "erhe_file")
add_library(${_target})
add_library(erhe::file ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_file/file.cpp
    erhe_file/file.hpp
    erhe_file/file_log.cpp
    erhe_file/file_log.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PRIVATE
        erhe::defer
        erhe::log
        erhe::verify
)

if (ERHE_TARGET_OS_ANDROID)
    # SDL_IOFromFile routes asset paths through AAssetManager on Android.
    target_link_libraries(${_target} PRIVATE SDL3::SDL3-static)
endif ()

erhe_target_settings(${_target} "erhe")

if (${ERHE_BUILD_TESTS} STREQUAL "ON")
    add_subdirectory(test)
endif ()
