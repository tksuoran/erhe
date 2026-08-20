CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_scene_tests")
add_executable(${_target}
    main.cpp
    test_animation_apply.cpp
    test_animation_sampler.cpp
    test_light_frame.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::scene
        erhe::log
        erhe::math
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
