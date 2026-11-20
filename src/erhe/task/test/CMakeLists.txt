set(_target "erhe_task_tests")
add_executable(${_target}
    main.cpp
    test_executor.cpp
    test_executors.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::file
        erhe::log
        erhe::task
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
