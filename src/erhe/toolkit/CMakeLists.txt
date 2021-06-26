set(_target "erhe_toolkit")

set(source_list
    file.cpp
    file.hpp
    hidapi.c
    hidapi.h
    math_util.cpp
    math_util.hpp
    space_mouse.cpp
    space_mouse.hpp
    tracy_client.hpp
    verify.hpp
    view.cpp
    view.hpp
    window.hpp
)

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "GLFW")
    list(APPEND source_list
         glfw_window.cpp
         glfw_window.hpp
    )
endif()

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
    list(APPEND source_list
         mango_window.cpp
         mango_window.hpp
    )
endif()

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        erhe_gl
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "GLFW")
    target_link_libraries(${_target} PUBLIC glfw)
endif ()

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
    target_link_libraries(${_target} PUBLIC mango)
endif()

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
