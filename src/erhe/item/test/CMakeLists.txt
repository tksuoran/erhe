CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_item_tests")
add_executable(${_target}
    main.cpp
    test_unique_id.cpp
    test_item_flags.cpp
    test_item_filter.cpp
    test_item_base.cpp
    test_item_crtp.cpp
    test_hierarchy.cpp
    test_item_host.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::item
        erhe::file
        erhe::log
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
