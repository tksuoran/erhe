set(_target "erhe_profile")
add_library(${_target})
add_library(erhe::profile ALIAS ${_target})
erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    erhe_profile/profile.cpp
    erhe_profile/profile.hpp
)
target_include_directories(${_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (${ERHE_USE_PRECOMPILED_HEADERS})
    target_precompile_headers(${_target} REUSE_FROM erhe_pch)
endif ()
erhe_target_settings(${_target})
if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PUBLIC ${ERHE_PROFILE_TARGET})
endif ()
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
