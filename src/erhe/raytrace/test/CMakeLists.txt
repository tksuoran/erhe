CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_raytrace_tests")
add_executable(${_target}
    main.cpp
    test_helpers.hpp
    test_geometry.cpp
    test_hierarchy.cpp
    test_masking.cpp
    test_instance.cpp
    test_scene.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::raytrace
        erhe::buffer
        erhe::dataformat
        erhe::file
        erhe::log
        erhe::verify
        GTest::gtest
        glm::glm-header-only
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
