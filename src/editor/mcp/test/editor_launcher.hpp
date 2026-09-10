#pragma once

// Editor process management for mcp_server_tests.
//
// mcp_server_tests is a pure HTTP client of the MCP server embedded in the
// editor. Under ctest the editors are started and stopped by the CTest
// fixtures in CMakeLists.txt (one plain editor for Mcp_test.*, one with a
// bearer token for Mcp_auth_test.*). Visual Studio's Test Explorer runs the
// gtest binary directly (no ctest, no fixture), so the binary launches the
// editor it needs itself when nothing answers on the port: the editor path,
// the repo root (config/, res/, logs/) and the test token file are compiled
// in from CMake as ERHE_MCP_TEST_EDITOR_PATH / ERHE_MCP_TEST_REPO_ROOT /
// ERHE_MCP_TEST_TOKEN_FILE.

#include <string>

namespace mcp_test {

// The token file CMake wrote for the auth fixture, or empty when not
// compiled in.
auto compiled_in_token_file() -> std::string;

// Trimmed contents of a token file; empty when the path is empty or the
// file cannot be read.
auto read_token_file(const std::string& path) -> std::string;

// True when an MCP server answers GET /health at host:port.
auto is_editor_reachable(const std::string& host, int port) -> bool;

// Spawns the editor detached (working directory = repo root, ERHE_MCP_PORT =
// port, ERHE_AI_DRIVER=1, and ERHE_MCP_TOKEN_FILE = token_file when that is
// not empty) and waits up to timeout_seconds for GET /health. Returns
// false, with the reason on stderr, when the editor path is not compiled
// in, the spawn fails or the server does not come up in time (the spawned
// process is terminated in that case). token is the token to present when
// stopping that editor later (the file contents; empty for no auth).
auto launch_editor(const std::string& host, int port, int timeout_seconds, const std::string& token_file, const std::string& token) -> bool;

// Calls the request_exit MCP tool on host:port (presenting token as the
// bearer token when not empty) and waits until /health stops answering (at
// most 60 s). Returns a process exit code: 0 when the editor is gone (or
// none was there), 1 otherwise. Only the editor on that port is touched -
// never another editor instance.
auto request_exit_and_wait(const std::string& host, int port, const std::string& token) -> int;

// Stops every editor started by launch_editor(): request_exit_and_wait,
// then a hard terminate for one still alive. No-op when none was launched.
void stop_launched_editors();

} // namespace mcp_test
