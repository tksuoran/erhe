CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_primitive_tests")
add_executable(${_target}
    main.cpp
    test_attribute_encodings.cpp
    test_material_authored_values.cpp
    test_material_style.cpp
    test_material_textures.cpp
    test_material_prim.cpp
    test_mesh_optimizer.cpp
    test_optimized_variant_build.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::primitive
        erhe::buffer
        erhe::dataformat
        erhe::file
        erhe::geometry
        erhe::graphics
        erhe::item
        erhe::log
        erhe::property
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
