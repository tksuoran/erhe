set(_target "erhe_codegen_test")
add_executable(${_target})

erhe_target_sources_grouped(
    ${_target} TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    test_migration.cpp
    generated/settings.hpp
    generated/settings.cpp
)

target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(${_target}
    PRIVATE
        erhe::codegen
)

erhe_target_settings(${_target} "erhe")
