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
    test_collision_filter_properties.cpp
    test_joint_reach.cpp
    test_overlap_query.cpp
)

# Box3d_world internals (collision filter interning, hull point generation,
# six-DOF joint classification, shape descriptors) and the activation and sensor
# events that Box3d_world synthesizes itself.
if (${ERHE_PHYSICS_LIBRARY} STREQUAL "box3d")
    target_sources(${_target}
        PRIVATE
            test_collision_filter_table.cpp
            test_hull_builder.cpp
            test_shape_descriptor.cpp
            test_six_dof_classifier.cpp
            test_world_events.cpp
    )
endif ()

target_link_libraries(${_target}
    PRIVATE
        erhe::physics
        erhe::file
        erhe::item
        erhe::property
        erhe::log
        erhe::profile
        erhe::verify
        glm::glm-header-only
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
