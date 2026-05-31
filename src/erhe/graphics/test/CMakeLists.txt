CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

include(GoogleTest)

# Deviceless tests: pure std140/std430 layout math, no graphics Device.
# Built in every configuration (CI-friendly, GPU-less).
set(_deviceless_target "erhe_graphics_tests")
add_executable(${_deviceless_target}
    main.cpp
    test_shader_resource_size.cpp
)

target_link_libraries(${_deviceless_target}
    PRIVATE
        erhe::graphics
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_deviceless_target} "erhe/tests")

gtest_discover_tests(${_deviceless_target})

# Real-GPU tests: construct a headless Device and run actual render / compute
# work, then read results back and assert. Only added when the configuration
# can bring up a headless Device: the null Context_window exists only when
# ERHE_WINDOW_LIBRARY=none, and today only the Vulkan backend has a validated
# headless path. Widen the API list as other backends gain one.
set(_gpu_tests_supported OFF)
if ((ERHE_WINDOW_LIBRARY STREQUAL "none") AND (ERHE_GRAPHICS_API STREQUAL "vulkan"))
    set(_gpu_tests_supported ON)
endif ()

if (_gpu_tests_supported)
    set(_gpu_target "erhe_graphics_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
        gpu_test_environment.cpp
        gpu_test_fixture.cpp
        test_m1_device_up.cpp
        test_m2_clear_color.cpp
        test_m3_triangle.cpp
        test_m4_compute_ssbo.cpp
        test_m5_blend.cpp
        test_m5_depth.cpp
        test_buffer_transfer.cpp
        test_raster_state.cpp
        test_mrt.cpp
        test_texture_sample.cpp
        test_vertex_index.cpp
        test_compute_ubo.cpp
        test_instanced.cpp
        test_texture_upload.cpp
        test_compute_2d.cpp
        test_topology.cpp
        test_stencil.cpp
        test_copy.cpp
        test_load_action.cpp
        test_blend_premultiplied.cpp
        test_sampler_modes.cpp
        test_depth_readback.cpp
        test_float32_render.cpp
        test_snorm_render.cpp
        test_struct_types.cpp
        test_msaa_resolve.cpp
        test_device_caps.cpp
    )

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::graphics
            erhe::window
            erhe::dataformat
            erhe::verify
            GTest::gtest
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # The binary creates a VkInstance, so it must not be executed at build
    # time on a GPU-less builder. Defer enumeration to ctest time.
    gtest_discover_tests(${_gpu_target} DISCOVERY_MODE PRE_TEST)
else ()
    message(STATUS "erhe_graphics_gpu_tests skipped (needs ERHE_WINDOW_LIBRARY=none + ERHE_GRAPHICS_API=vulkan)")
endif ()
