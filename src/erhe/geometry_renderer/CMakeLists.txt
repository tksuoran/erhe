set(_target "erhe_geometry_renderer")
add_library(${_target})
add_library(erhe::geometry_renderer ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_geometry_renderer/geometry_debug_renderer.cpp
    erhe_geometry_renderer/geometry_debug_renderer.hpp
)
target_link_libraries(
    ${_target}
    PUBLIC
        erhe::geometry
        erhe::math
        erhe::renderer
        erhe::profile
        erhe::verify
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
