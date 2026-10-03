# Device-free glTF tests: parse_gltf creates no GPU objects and export_gltf
# returns a string, so build -> export -> parse -> compare runs without an
# editor or a graphics device.
set(_target "erhe_gltf_tests")
add_executable(${_target}
    main.cpp
    gltf_test_util.cpp
    gltf_test_util.hpp
    test_gltf_parse.cpp
    test_gltf_roundtrip.cpp
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
        Taskflow
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
