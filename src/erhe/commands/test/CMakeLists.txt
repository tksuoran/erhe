set(_target "erhe_commands_tests")
add_executable(${_target}
    main.cpp
    test_binding_desc.cpp
    test_binding_overrides.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::commands
        erhe::file
        erhe::log
        erhe::verify
        erhe::window
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
