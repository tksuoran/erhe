CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_frame_pacing_tests")
add_executable(${_target}
    main.cpp
    frame_pacing_plant.hpp
    test_frame_pacer_claims.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::frame_pacing
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})

# Standalone timer wake-up jitter probe (implementation plan step P0.5):
# grounds the frame pacer 'guard' tunable on the target machine.
add_executable(erhe_frame_pacing_timer_probe timer_jitter_probe.cpp)
erhe_target_settings(erhe_frame_pacing_timer_probe "erhe/tests")
