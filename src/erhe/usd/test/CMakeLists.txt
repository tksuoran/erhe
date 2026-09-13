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
    test_usd_arc_supplied_xform_ops.cpp
    test_usd_attribute_samples.cpp
    test_usd_attribute_splines.cpp
    test_usd_brushes.cpp
    test_usd_class_prototypes.cpp
    test_usd_defined.cpp
    test_usd_double_sided.cpp
    test_usd_export.cpp
    test_usd_folders.cpp
    test_usd_geometry_graphs.cpp
    test_usd_hole_faces.cpp
    test_usd_image_header.cpp
    test_usd_import.cpp
    test_usd_materials.cpp
    test_usd_node_graphs.cpp
    test_usd_open_pbr.cpp
    test_usd_primitives.cpp
    test_usd_physics.cpp
    test_usd_physics_export.cpp
    test_usd_point_instancers.cpp
    test_usd_prims.cpp
    test_usd_references.cpp
    test_usd_references_variants.cpp
    test_usd_skinning.cpp
    test_usd_styles.cpp
    test_usd_sublayers.cpp
    test_usd_texture_channels.cpp
    test_usd_texture_inputs.cpp
    test_usd_time_samples.cpp
    test_usd_variant_arcs.cpp
    test_usd_variants.cpp
    test_usd_xform_ops.cpp
)

target_compile_definitions(${_target}
    PRIVATE
        ERHE_USD_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
)

# `spline-eval.hh` is the USD spline evaluator the Ts spline test checks the
# reader's tangent conversion against; it is header-only, so the directory is
# all the test needs. LightUSD's headers are not clean at erhe's warning
# level, so it is a SYSTEM include, the way erhe_usd itself names it.
target_include_directories(${_target} SYSTEM PRIVATE ${ERHE_LIGHTUSD_INCLUDE_DIR})

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
