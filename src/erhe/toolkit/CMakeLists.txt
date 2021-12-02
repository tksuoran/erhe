set(_target "erhe_toolkit")

set(source_list
    file.cpp
    file.hpp
    math_util.cpp
    math_util.hpp
    profile.hpp
    verify.hpp
    view.cpp
    view.hpp
    window.hpp
)

if (CMAKE_SYSTEM_NAME MATCHES "(Windows|MSYS)")
    list(APPEND source_list
        space_mouse.cpp
        space_mouse.hpp
    )
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    list(APPEND source_list
         glfw_window.cpp
         glfw_window.hpp
    )
endif()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "mango")
    list(APPEND source_list
         mango_window.cpp
         mango_window.hpp
    )
endif()

add_library(${_target} ${source_list})

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
    erhe_gl
    fmt::fmt
    glm::glm
    Microsoft.GSL::GSL
)

if (CMAKE_SYSTEM_NAME MATCHES "(Windows|MSYS)")
    target_link_libraries(
        ${_target}
        PUBLIC
        hidapi
    )
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "glfw")
    target_link_libraries(${_target} PUBLIC glfw)
endif ()

if (${ERHE_WINDOW_LIBRARY} STREQUAL "mango")
    target_link_libraries(${_target} PUBLIC mango)
endif()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "tracy")
    target_link_libraries(${_target} PRIVATE TracyClient)
endif ()

if (${ERHE_PROFILE_LIBRARY} STREQUAL "superluminal")
    target_link_libraries(${_target} PRIVATE SuperluminalAPI)
endif ()

target_strict_cxx(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")

foreach(source IN LISTS source_list)
    get_filename_component(source_path "${source}" PATH)
    string(REPLACE "/" "\\" source_path_msvc "${source_path}")
    source_group("${source_path_msvc}" FILES "${source}")
endforeach()
