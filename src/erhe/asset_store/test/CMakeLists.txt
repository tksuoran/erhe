set(_target "erhe_asset_store_tests")
add_executable(${_target}
    main.cpp
    test_asset_store.cpp
    test_key.cpp
    test_read_throughput.cpp
)

target_link_libraries(${_target}
    PRIVATE
        erhe::asset_store
        erhe::file
        erhe::log
        GTest::gtest
)

erhe_target_settings(${_target} "erhe/tests")

include(GoogleTest)
gtest_discover_tests(${_target})
add_dependencies(erhe_tests ${_target})
