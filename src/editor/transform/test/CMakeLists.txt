CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "editor_ik_solver_tests")
add_executable(${_target}
    main.cpp
    # The editor source under test is compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against. ik_solver.cpp is pure (glm + std only).
    ${CMAKE_CURRENT_SOURCE_DIR}/../ik_solver.cpp
    test_ik_solver.cpp
)

# "transform/ik_solver.hpp" is reached relative to the editor source root.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

target_link_libraries(${_target}
    PRIVATE
        glm::glm-header-only
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
