CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_usd_tests")
add_executable(${_target}
    main.cpp
    test_usd_active.cpp
    test_usd_brushes.cpp
    test_usd_class_prototypes.cpp
    test_usd_double_sided.cpp
    test_usd_export.cpp
    test_usd_import.cpp
    test_usd_materials.cpp
    test_usd_primitives.cpp
    test_usd_point_instancers.cpp
    test_usd_prims.cpp
    test_usd_references.cpp
    test_usd_styles.cpp
    test_usd_sublayers.cpp
    test_usd_texture_channels.cpp
    test_usd_texture_inputs.cpp
    test_usd_time_samples.cpp
    test_usd_variants.cpp
    test_usd_xform_ops.cpp
)

target_compile_definitions(${_target}
    PRIVATE
        ERHE_USD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

target_link_libraries(${_target}
    PRIVATE
        erhe::dataformat
        erhe::file
        erhe::geometry
        erhe::item
        erhe::log
        erhe::primitive
        erhe::property
        erhe::scene
        erhe::usd
        erhe::verify
        geogram
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
