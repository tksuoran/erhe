# Rendergraph needs a Device (frames in flight, the frame index), so its
# tests are device-backed. erhe::gpu_test_support is defined by
# src/erhe/graphics/test only where a Device can actually be brought up for
# the configured (api, window) pair, so its existence IS the support
# condition - erhe/graphics is configured before this directory.
if (TARGET erhe::gpu_test_support)
    set(_gpu_target "erhe_rendergraph_gpu_tests")
    add_executable(${_gpu_target}
        main.cpp
        test_rendergraph_pre_execute.cpp
    )

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::gpu_test_support
            erhe::file
            erhe::graph
            erhe::graphics
            erhe::item
            erhe::log
            erhe::property
            erhe::rendergraph
            erhe::utility
            erhe::verify
            GTest::gtest
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # Needs a Device: labeled "gpu" so a device-less machine can skip it.
    include(GoogleTest)
    gtest_discover_tests(${_gpu_target} PROPERTIES LABELS "gpu")
    add_dependencies(erhe_tests ${_gpu_target})
endif ()
