set(_target "erhe_math_tests")
add_executable(${_target}
    main.cpp
    test_euler_angles.cpp
    test_projection.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::math
        erhe::profile
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
