set(_target "erhe_file_tests")
add_executable(${_target}
    main.cpp
    test_positional_file_reader.cpp
    test_write_file_via_temp.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::file
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
