set(_target "editor_navigation_tests")
add_executable(${_target}
    main.cpp
    # The editor sources under test are compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. The navigation sources need Recast, Detour,
    # glm, the executor and the editor log only.
    ${CMAKE_CURRENT_SOURCE_DIR}/../navigation_build.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../navigation_mesh.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../navigation_mesh_io.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../editor_log.cpp
    test_navigation.cpp
)

# "navigation/navigation_mesh.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
target_compile_definitions(${_target} PRIVATE ERHE_USE_DETOUR ERHE_USE_RECAST)

target_link_libraries(${_target}
    PRIVATE
        erhe::file
        erhe::log
        erhe::task
        fmt::fmt
        glm::glm-header-only
        GTest::gtest
        RecastNavigation::Detour
        RecastNavigation::Recast
        Taskflow
)

erhe_target_settings(${_target} "erhe/tests")

add_dependencies(erhe_tests ${_target})

include(GoogleTest)
gtest_discover_tests(${_target})
