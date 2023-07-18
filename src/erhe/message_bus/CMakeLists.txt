set(_target "erhe_message_bus")
add_library(${_target})
add_library(erhe::message_bus ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    message_bus.cpp
    message_bus.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(
    ${_target}
    PRIVATE
        erhe::log
        fmt::fmt
        Microsoft.GSL::GSL
    PUBLIC
        glm::glm
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
