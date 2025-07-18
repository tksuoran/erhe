set(_target "erhe_math")
add_library(${_target})
add_library(erhe::math ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_math/aabb.cpp
    erhe_math/aabb.hpp
    erhe_math/input_axis.cpp
    erhe_math/input_axis.hpp
    erhe_math/math_log.cpp
    erhe_math/math_log.hpp
    erhe_math/math_util.cpp
    erhe_math/math_util.hpp
    erhe_math/sphere.cpp
    erhe_math/sphere.hpp
    erhe_math/viewport.cpp
    erhe_math/viewport.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(
    ${_target}
    PUBLIC
        glm::glm-header-only
    PRIVATE
        erhe::dataformat
        erhe::log
        erhe::profile
        erhe::verify
        quickhull::quickhull
)

erhe_target_settings(${_target} "erhe")
