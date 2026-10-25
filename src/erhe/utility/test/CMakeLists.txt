set(_target "erhe_utility_tests")
add_executable(${_target}
    test_memory_budget.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::utility
        GTest::gtest_main
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
