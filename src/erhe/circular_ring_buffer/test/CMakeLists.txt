CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_circular_ring_buffer_tests")
add_executable(${_target}
    main.cpp
    test_circular_ring_buffer_algorithm.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::circular_ring_buffer
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
