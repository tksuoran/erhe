CPMAddPackage(
    NAME              googletest
    VERSION           1.16.0
    GIT_SHALLOW       TRUE
    GITHUB_REPOSITORY google/googletest
    OPTIONS
        "BUILD_GMOCK OFF"
        "INSTALL_GTEST OFF"
)

set(_target "editor_asset_tests")
add_executable(${_target}
    main.cpp
    # The editor source under test is compiled directly into the test
    # executable: the editor itself is an executable, so there is no editor
    # library to link against.
    ${CMAKE_CURRENT_SOURCE_DIR}/../pending_item_removals.cpp
    test_pending_item_removals.cpp
)

# app_message.hpp (for Removed_items) is reached as "app_message.hpp", which
# only the editor target has on its include path.
target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)

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
