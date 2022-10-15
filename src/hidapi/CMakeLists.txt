set(_target "hidapi")

if (ERHE_TARGET_OS_WINDOWS)
    add_library(${_target}
        hidapi.h
        hidapi_windows.c
    )
elseif (ERHE_TARGET_OS_LINUX)
    add_library(${_target}
        hidapi.h
        hidapi_linux.c
    )
    target_link_libraries(${_target} udev)
endif ()


target_include_directories(${_target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
