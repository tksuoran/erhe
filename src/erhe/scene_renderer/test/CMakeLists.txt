CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

# Deviceless: the membership and slot logic of Material_set is pure
# bookkeeping over shared_ptr<Material>, so it is tested with no window, no
# device, no scene and no draw list (doc/erhe/draw_list_material_set.md R16).
set(_target "erhe_scene_renderer_tests")
add_executable(${_target}
    main.cpp
    test_material_set.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::scene_renderer
        erhe::primitive
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})

# Device-backed half (V2). erhe::gpu_test_support is defined by
# src/erhe/graphics/test only where a Device can actually be brought up for the
# configured (api, window) pair, so its existence IS the support condition -
# erhe/graphics is configured before this directory.
if (TARGET erhe::gpu_test_support)
    set(_gpu_target "erhe_scene_renderer_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
        test_content_line_width_gpu.cpp
        test_material_set_gpu.cpp
    )

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::gpu_test_support
            erhe::scene_renderer
            erhe::primitive
            erhe::scene
            erhe::item
            erhe::property
            erhe::verify
            GTest::gtest
    )

    # The content wide-line test compiles the editor's line shaders from
    # res/shaders/ in the repository.
    target_compile_definitions(${_gpu_target}
        PRIVATE
            "ERHE_SCENE_RENDERER_TEST_REPO_ROOT=\"${PROJECT_SOURCE_DIR}\""
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # Needs a Device: labeled "gpu" so a device-less machine can skip it.
    gtest_discover_tests(${_gpu_target} PROPERTIES LABELS "gpu")
    add_dependencies(erhe_tests ${_gpu_target})
endif ()
