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
add_dependencies(erhe_tests ${_deviceless_target})

# Real-GPU tests: construct a Device and run actual render / compute work, then
# read results back and assert. The tests render to offscreen textures and never
# present to a swapchain, so they run identically headless or windowed; the only
# requirement is that a Device can be brought up for the configured (api, window)
# pair. The bring-up is the same generic Context_window + Surface + Device path
# the editor uses (a hidden show=false window suffices when a window is needed):
#   - Vulkan brings up a Device with no window (ERHE_WINDOW_LIBRARY=none ->
#     surfaceless headless) OR with a real window, so it is supported for any
#     window library.
#   - OpenGL and Metal need a real context/surface, which requires a real window
#     library (sdl/glfw); they have no validated windowless path, so
#     ERHE_WINDOW_LIBRARY=none is not supported for them.
set(_gpu_tests_supported OFF)
if (ERHE_GRAPHICS_API STREQUAL "vulkan")
    set(_gpu_tests_supported ON)
elseif ((ERHE_GRAPHICS_API STREQUAL "opengl") OR (ERHE_GRAPHICS_API STREQUAL "metal"))
    if (NOT (ERHE_WINDOW_LIBRARY STREQUAL "none"))
        set(_gpu_tests_supported ON)
    endif ()
endif ()

if (_gpu_tests_supported)
    # The device bring-up and the per-test frame / readback helpers are shared
    # with the other libraries' GPU test targets (erhe_scene_renderer_tests'
    # device-backed half), so they are a library rather than sources compiled
    # into this executable. It is defined here because this is where the
    # device-support condition above is decided; erhe/graphics is configured
    # before every library that consumes it.
    set(_gpu_support_target "erhe_gpu_test_support")
    add_library(${_gpu_support_target} STATIC
        gpu_test_environment.cpp
        gpu_test_environment.hpp
        gpu_test_fixture.cpp
        gpu_test_fixture.hpp
    )
    target_link_libraries(${_gpu_support_target}
        PUBLIC
            erhe::graphics
            erhe::window
            erhe::dataformat
            GTest::gtest
        PRIVATE
            erhe::log
            erhe::verify
    )
    target_include_directories(${_gpu_support_target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    erhe_target_settings(${_gpu_support_target} "erhe/tests")
    add_library(erhe::gpu_test_support ALIAS ${_gpu_support_target})

    set(_gpu_target "erhe_graphics_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
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
        test_copy_texture.cpp
        test_load_action.cpp
        test_blend_premultiplied.cpp
        test_sampler_modes.cpp
        test_depth_readback.cpp
        test_float32_render.cpp
        test_snorm_render.cpp
        test_struct_types.cpp
        test_msaa_resolve.cpp
        test_device_caps.cpp
        test_mipmaps.cpp
        test_texture_array.cpp
        test_texture_3d.cpp
        test_texture_cube.cpp
        test_texgen_render.cpp
        test_worker_context.cpp
        test_multi_copy_buffer.cpp
    )

    # GL-specific worker-context tests: worker-side texture upload and
    # context-index introspection reach GL internals (Device::get_impl(),
    # erhe_graphics/gl headers, raw gl:: calls), so they exist only on the
    # OpenGL backend.
    if (ERHE_GRAPHICS_API STREQUAL "opengl")
        target_sources(${_gpu_target} PRIVATE test_worker_context_gl.cpp)
        target_link_libraries(${_gpu_target} PRIVATE erhe::gl)
    endif ()

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::gpu_test_support
            erhe::graphics
            erhe::texgen
            erhe::window
            erhe::dataformat
            erhe::verify
            GTest::gtest
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # The binary creates a VkInstance, so it must not be executed at build
    # time on a GPU-less builder. Defer enumeration to ctest time. The "gpu"
    # label lets a device-less machine leave these out (ctest -LE gpu).
    gtest_discover_tests(${_gpu_target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "gpu"
    )
    add_dependencies(erhe_tests ${_gpu_target})
else ()
    message(STATUS "erhe_graphics_gpu_tests skipped: no Device bring-up path for ERHE_GRAPHICS_API='${ERHE_GRAPHICS_API}' + ERHE_WINDOW_LIBRARY='${ERHE_WINDOW_LIBRARY}' (need vulkan with any window library, or opengl/metal with a non-none window library)")
endif ()
