CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_texgen_tests")
add_executable(${_target}
    main.cpp
    test_descriptors.hpp
    test_assembly.cpp
    test_inputs.cpp
    test_substitution.cpp
    test_value_type.cpp
    test_variants.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::texgen
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
