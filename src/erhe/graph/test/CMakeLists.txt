CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "erhe_graph_tests")
add_executable(${_target}
    main.cpp
    test_node.hpp
    test_graph_connect.cpp
    test_graph_edit.cpp
    test_graph_sort.cpp
    test_node_copy.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::graph
        erhe::item
        erhe::file
        erhe::log
        erhe::verify
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
