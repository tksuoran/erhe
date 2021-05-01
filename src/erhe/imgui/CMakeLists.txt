set(source_list
    imgui_impl_erhe.cpp
    imgui_impl_erhe.hpp
)

add_library(erhe_imgui ${source_list})

add_library(erhe::imgui ALIAS erhe_imgui)

target_include_directories(erhe_imgui
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(erhe_imgui
    PUBLIC
        erhe::graphics
        imgui
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
