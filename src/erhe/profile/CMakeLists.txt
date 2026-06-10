set(_target "erhe_profile")
add_library(${_target})
add_library(erhe::profile ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_profile/profile.cpp
    erhe_profile/profile.hpp
)

target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (ERHE_USE_MIMALLOC)
    target_link_libraries(${_target} PUBLIC mimalloc-static)
endif ()

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PUBLIC ${ERHE_PROFILE_TARGET})
endif ()

if (ERHE_GRAPHICS_API_VULKAN)
    target_link_libraries(${_target} PUBLIC volk::volk)
endif ()

# profile.hpp's OpenGL Tracy-GPU backend includes erhe_gl/dynamic_load.hpp and
# aliases the gl* timestamp-query entry points to gl::*, so erhe_profile needs
# erhe::gl (its headers plus the dynamically-loaded symbols) on the OpenGL
# backend -- the mirror of the volk::volk dependency above for Vulkan. erhe_gl
# does not link erhe_profile, so this introduces no dependency cycle.
if (ERHE_GRAPHICS_API_OPENGL)
    target_link_libraries(${_target} PUBLIC erhe::gl)
endif ()


erhe_target_settings(${_target} "erhe")
