CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "mcp_server_tests")
add_executable(${_target}
    main.cpp
    mcp_server_tests.cpp
)

target_link_libraries(${_target}
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
        GTest::gtest
)

set_target_properties(
    ${_target} PROPERTIES
    CXX_STANDARD          20
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS        NO
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
