set(source_list
    command_buffer.cpp
    command_buffer.hpp
    debug.cpp
    debug.hpp
    image_transfer.cpp
    image_transfer.hpp
    #render_group.cpp # WIP
    #render_group.hpp
    # samplers.cpp removed
    # samplers.hpp
    shader_monitor.cpp
    shader_monitor.hpp
    # storage_buffer_range.cpp removed?
    # storage_buffer_range.hpp
    texture_bindings.cpp
    texture_bindings.hpp
)

add_library(erhe_graphics_experimental ${source_list})

add_library(erhe::graphics_experimental ALIAS erhe_graphics_experimental)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCE_LIST})

target_include_directories(erhe_graphics_experimental
    PUBLIC
        ${ERHE_INCLUDE_ROOT}
)

target_link_libraries(erhe_graphics_experimental
    PUBLIC
        erhe::graphics
        erhe::gl
        erhe::log
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
        TracyClient::TracyClient
)

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
