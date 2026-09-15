CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_dataformat_tests")
add_executable(${_target}
    main.cpp
    test_snorm16_quantization.cpp
    test_vertex_format.cpp
    test_vertex_stream_packing.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::dataformat
        erhe::verify
        # test_snorm16_quantization.cpp pins meshopt_quantizeSnorm() against
        # erhe::dataformat::float_to_snorm16(). The invariant belongs to
        # erhe_primitive, which is the target that uses both, but erhe_primitive
        # has no test target of its own yet - so the test lives next to the
        # function it constrains. erhe_dataformat itself does not depend on
        # meshoptimizer, and this link is PRIVATE and test-only.
        meshoptimizer
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
