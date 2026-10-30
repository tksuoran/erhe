# Device-free glTF tests: parse_gltf creates no GPU objects and export_gltf
# returns a string, so build -> export -> parse -> compare runs without an
# editor or a graphics device.
set(_target "erhe_gltf_tests")
add_executable(${_target}
    main.cpp
    gltf_test_util.cpp
    gltf_test_util.hpp
    test_gltf_parse.cpp
    test_gltf_pvs.cpp
    test_gltf_roundtrip.cpp
    test_gltf_soup_reload.cpp
)

target_compile_definitions(${_target} PRIVATE ERHE_GLTF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

target_link_libraries(${_target}
    PRIVATE
        erhe::file
        erhe::geometry
        erhe::gltf
        erhe::graphics
        erhe::item
        erhe::log
        erhe::math
        erhe::primitive
        erhe::property
        erhe::raytrace
        erhe::scene
        GTest::gtest
        simdjson::simdjson
        Taskflow
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})

# Device-backed half: exporting a material texture slot needs an
# erhe::graphics::Texture, which needs a Device. erhe::gpu_test_support is
# defined by src/erhe/graphics/test only where a Device can be brought up for
# the configured (api, window) pair, so its existence IS the support
# condition - erhe/graphics is configured before this directory.
if (TARGET erhe::gpu_test_support)
    set(_gpu_target "erhe_gltf_gpu_tests")
    add_executable(${_gpu_target}
        gpu_main.cpp
        gltf_test_util.cpp
        gltf_test_util.hpp
        test_gltf_texture_gpu.cpp
    )

    target_compile_definitions(${_gpu_target} PRIVATE ERHE_GLTF_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

    target_link_libraries(${_gpu_target}
        PRIVATE
            erhe::gpu_test_support
            erhe::file
            erhe::geometry
            erhe::gltf
            erhe::graphics
            erhe::item
            erhe::log
            erhe::math
            erhe::primitive
            erhe::property
            erhe::raytrace
            erhe::scene
            GTest::gtest
            simdjson::simdjson
            Taskflow
    )

    erhe_target_settings(${_gpu_target} "erhe/tests")

    # Needs a Device: labeled "gpu" so a device-less machine can skip it
    # (cmake/functions.cmake).
    erhe_add_gpu_test(${_gpu_target})
    add_dependencies(erhe_tests ${_gpu_target})
endif ()
