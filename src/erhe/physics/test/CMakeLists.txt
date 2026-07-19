CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_physics_tests")
add_executable(${_target}
    main.cpp
    test_body_activation.cpp
    test_collision_filter_table.cpp
    test_hull_builder.cpp
    test_shape_descriptor.cpp
    test_six_dof_classifier.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::physics
        erhe::file
        erhe::item
        erhe::log
        erhe::physics
        erhe::profile
        erhe::verify
        glm::glm-header-only
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
