set(_target "mikktspace")

set(source_list
    mikktspace.h
    mikktspace.c
)

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
