CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_geometry_tests")
add_executable(${_target}
    main.cpp
    test_capsule.cpp
    test_chamfer_diagnostics.cpp
    test_chamfer_self_intersection.cpp
    test_component_selection_remap.cpp
    test_csg.cpp
    test_geometry_operation.cpp
    test_plane_intersection.cpp
    test_selective_operation_normals.cpp
    test_timing_harness.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::geometry
        erhe::log
        erhe::math
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
