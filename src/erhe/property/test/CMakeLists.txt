CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_property_tests")
add_executable(${_target}
    main.cpp
    test_object.hpp
    test_registry.cpp
    test_secondary_owner.cpp
    test_sealing.cpp
    test_style.cpp
    test_dependency_object.cpp
    test_inheritance.cpp
    test_observers.cpp
    test_enum.cpp
    test_property_string.cpp
    test_property_set.cpp
    test_bridge.cpp
    test_computed.cpp
    test_computed_default.cpp
    test_default_elision.cpp
    test_expressions.cpp
    test_object_reference.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::property
        erhe::file
        erhe::log
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
