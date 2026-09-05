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
    test_usd_import.cpp
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
