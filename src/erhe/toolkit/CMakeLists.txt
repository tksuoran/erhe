set(source_list
    file.cpp
    file.hpp
    math_util.cpp
    math_util.hpp
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

add_library(erhe_toolkit ${source_list})

add_library(erhe::toolkit ALIAS erhe_toolkit)

target_include_directories(erhe_toolkit PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(erhe_toolkit
    PUBLIC
        erhe::gl
        fmt::fmt
        glm::glm
        Microsoft.GSL::GSL
)

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "GLFW")
    target_link_libraries(erhe_toolkit PUBLIC glfw)
endif ()

if (${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
    target_link_libraries(erhe_toolkit PUBLIC mango)
endif()

#if (${ERHE_WINDOW_TOOLKIT} STREQUAL "Mango")
#endif ()

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
