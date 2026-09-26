CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

# Device-backed only: Debug_renderer compiles its shaders and draws. The
# erhe::gpu_test_support target exists only where a Device can be brought up
# for the configured (api, window) pair (src/erhe/graphics/test), so its
# existence IS the support condition.
if (TARGET erhe::gpu_test_support)
    set(_gpu_target "erhe_renderer_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
        test_debug_line_width_gpu.cpp
    )

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::gpu_test_support
            erhe::renderer
            erhe::graphics
            erhe::log
            erhe::verify
            GTest::gtest
    )

    # Debug_renderer loads res/shaders/ relative to the working directory;
    # main() changes to the repository root so every runner (ctest, Visual
    # Studio Test Explorer, a direct run) finds them.
    target_compile_definitions(${_gpu_target}
        PRIVATE
            "ERHE_RENDERER_TEST_REPO_ROOT=\"${PROJECT_SOURCE_DIR}\""
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # Needs a Device: labeled "gpu" so a device-less machine can skip it.
    gtest_discover_tests(${_gpu_target} PROPERTIES LABELS "gpu")
    add_dependencies(erhe_tests ${_gpu_target})
endif ()
