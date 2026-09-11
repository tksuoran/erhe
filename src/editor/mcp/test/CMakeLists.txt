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
    editor_launcher.cpp
    editor_launcher.hpp
    main.cpp
    mcp_server_tests.cpp
)

# Where the binary finds an editor to launch when none answers on the test
# port (Visual Studio's Test Explorer runs it without the ctest fixture
# below); see editor_launcher.hpp. Forward slashes on purpose: they are
# valid for CreateProcess and need no escaping in a macro.
target_compile_definitions(${_target}
    PRIVATE
        "ERHE_MCP_TEST_EDITOR_PATH=\"$<TARGET_FILE:editor>\""
        "ERHE_MCP_TEST_REPO_ROOT=\"${PROJECT_SOURCE_DIR}\""
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

# mcp_server_tests is an HTTP client of the MCP server embedded in the
# editor executable, so ctest runs editors as CTest fixtures around the
# client tests: `mcp_editor` (port 3773) is shared by every Mcp_test case;
# `mcp_editor_auth` (port 3774) is a dedicated editor started with the
# bearer token below for Mcp_auth_test. A fixture setup test must EXIT
# before the tests that require it run, and nothing in CMake itself can
# start a process without waiting for it, so the setup step is
# `mcp_server_tests --start-editor`: it spawns the editor detached (standard
# streams on the null device, so ctest does not wait for it), refuses when
# something already answers on the port, and exits once GET /health answers
# 200 (the editor answers 503 until its main loop serves requests). Each
# case still waits for /health itself (ERHE_MCP_TEST_TIMEOUT_S) so a hand-
# started editor works too. The cleanup step asks that editor - and only
# that editor - to exit through its request_exit MCP tool
# (`mcp_server_tests --request-editor-exit`). Without the fixtures (Visual
# Studio's Test Explorer runs the gtest binary directly) the binary launches
# the editors itself, see editor_launcher.hpp; the fixtures are the faster
# path under ctest because one editor serves every per-case test process.
# The ports are deliberately
# not the editor default (3743) so a developer's interactively running
# editor is neither reused nor disturbed. The editors run from the repo
# root (config/, res/, logs/); their console output is not captured
# (logs/log.txt is the record), because a child created with handle
# inheritance keeps ctest's output pipe open and ctest then waits for the
# editor to exit: on Windows that rules out `cmd /c start` and
# Start-Process with -RedirectStandardOutput (both measured), leaving the
# unredirected Start-Process, which goes through ShellExecute and inherits
# no handles. ctest quotes every COMMAND argument, so the values PowerShell
# must see as strings are single-quoted inside the argument.
add_dependencies(${_target} editor)

# The bearer token for the auth fixture. A throwaway secret: it only ever
# guards the test editor on the auth port. Mode 0600 because the editor
# refuses a token file with any other mode (POSIX).
set(_token_file "${CMAKE_CURRENT_BINARY_DIR}/mcp_test_token")
file(WRITE "${_token_file}" "erhe-mcp-test-token\n")
file(CHMOD "${_token_file}" PERMISSIONS OWNER_READ OWNER_WRITE)
target_compile_definitions(${_target} PRIVATE "ERHE_MCP_TEST_TOKEN_FILE=\"${_token_file}\"")

# erhe_add_mcp_editor_fixture(<fixture> <port> <environment list>)
# Adds mcp_server_tests_fixture_<fixture>_start / _stop with FIXTURES_SETUP
# / FIXTURES_CLEANUP <fixture>. The environment list is applied to both
# steps (the token file: the started editor reads it, the stop presents it).
function(erhe_add_mcp_editor_fixture fixture port environment)
    set(_start "mcp_server_tests_fixture_${fixture}_start")
    set(_stop  "mcp_server_tests_fixture_${fixture}_stop")
    add_test(
        NAME    ${_start}
        COMMAND ${_target} --start-editor
    )
    set_tests_properties(${_start} PROPERTIES
        FIXTURES_SETUP "${fixture}"
        ENVIRONMENT    "ERHE_MCP_TEST_PORT=${port};${environment}"
        TIMEOUT        240
    )
    add_test(
        NAME    ${_stop}
        COMMAND ${_target} --request-editor-exit
    )
    set_tests_properties(${_stop} PROPERTIES
        FIXTURES_CLEANUP "${fixture}"
        ENVIRONMENT      "ERHE_MCP_TEST_PORT=${port};${environment}"
        TIMEOUT          150
    )
endfunction()

erhe_add_mcp_editor_fixture(mcp_editor      3773 "")
erhe_add_mcp_editor_fixture(mcp_editor_auth 3774 "ERHE_MCP_TEST_TOKEN_FILE=${_token_file}")

include(GoogleTest)
# One ENVIRONMENT variable per call: gtest_discover_tests forwards
# PROPERTIES to a post-build `cmake -P GoogleTestAddTests.cmake` command as
# a flat list, and a ';' inside a value corrupts that command line. The
# first case of a run pays the /health wait (ERHE_MCP_TEST_TIMEOUT_S default
# 30 s) only for a hand-started editor; a fixture start returns ready.
gtest_discover_tests(${_target}
    TEST_FILTER "Mcp_test.*"
    PROPERTIES
        FIXTURES_REQUIRED "mcp_editor"
        # The client tests share one editor: run them one at a time.
        RESOURCE_LOCK     "mcp_editor"
        ENVIRONMENT       "ERHE_MCP_TEST_PORT=3773"
)
gtest_discover_tests(${_target}
    TEST_FILTER "Mcp_auth_test.*"
    PROPERTIES
        FIXTURES_REQUIRED "mcp_editor_auth"
        RESOURCE_LOCK     "mcp_editor_auth"
        ENVIRONMENT       "ERHE_MCP_TEST_AUTH_PORT=3774"
)
