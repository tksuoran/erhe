set(_target erhe_configuration)
add_library(${_target})
add_library(erhe::configuration ALIAS ${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    configuration.cpp
    configuration.hpp
    configuration_log.cpp
    configuration_log.hpp
)

target_include_directories(${_target} PUBLIC ${ERHE_INCLUDE_ROOT})

target_link_libraries(${_target}
    PUBLIC
        cxxopts
        fmt::fmt
        etl::etl
        erhe::log
        erhe::toolkit
        mINI
)

if (DEFINED ERHE_PROFILE_TARGET)
    target_link_libraries(${_target} PRIVATE ${ERHE_PROFILE_TARGET})
endif ()

erhe_target_settings(${_target})
set_property(TARGET ${_target} PROPERTY FOLDER "erhe")
