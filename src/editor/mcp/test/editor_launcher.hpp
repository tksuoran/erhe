#pragma once

// Editor process management for mcp_server_tests.
//
// mcp_server_tests is a pure HTTP client of the MCP server embedded in the
// editor. Under ctest the editors are started and stopped by the CTest
// fixtures in CMakeLists.txt (one plain editor for Mcp_test.*, one with a
// bearer token for Mcp_auth_test.*), both through this binary:
// `--start-editor` (start_editor_and_wait) and `--request-editor-exit`
// (request_exit_and_wait). Visual Studio's Test Explorer runs the gtest
// binary directly (no ctest, no fixture), so the binary also launches the
// editor it needs itself when nothing answers on the port. The editor path,
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

// Waits up to timeout_seconds for GET /health at host:port to answer 200.
// The editor answers 503 from the moment its HTTP thread is up until its
// main loop serves requests, so this is the only readiness test; a single
// probe would launch a second editor beside one that is still starting.
auto wait_for_editor(const std::string& host, int port, int timeout_seconds) -> bool;

// Spawns the editor detached (working directory = repo root, ERHE_MCP_PORT =
// port, ERHE_AI_DRIVER=1, and ERHE_MCP_TOKEN_FILE = token_file when that is
// not empty) and waits up to timeout_seconds for GET /health. Returns
// false, with the reason on stderr, when the editor path is not compiled
// in, the spawn fails or the server does not come up in time (the spawned
// process is terminated in that case). token is the token to present when
// stopping that editor later (the file contents; empty for no auth).
auto launch_editor(const std::string& host, int port, int timeout_seconds, const std::string& token_file, const std::string& token) -> bool;

// The FIXTURES_SETUP step: spawns the editor exactly as launch_editor does
// and returns once GET /health answers 200, leaving the editor running
// (nothing waits on it: the child is detached with its standard streams on
// the null device, so ctest does not wait for it either). Refuses, with the
// reason on stderr, when something already answers on host:port - a
// leftover editor would otherwise be joined by a second one. Returns a
// process exit code: 0 when the editor is ready, 1 otherwise.
auto start_editor_and_wait(const std::string& host, int port, int timeout_seconds, const std::string& token_file) -> int;

// Calls the request_exit MCP tool on host:port (presenting token as the
// bearer token when not empty) and waits until /health stops answering (at
// most 60 s). An editor that is still starting (/health 503) is given up to
// 60 s to become ready first, so a stop that runs right after a start does
// not leave it behind. Returns a process exit code: 0 when the editor is
// gone (or none was there), 1 otherwise. Only the editor on that port is
// touched - never another editor instance.
auto request_exit_and_wait(const std::string& host, int port, const std::string& token) -> int;

// Stops every editor started by launch_editor(): request_exit_and_wait,
// then a hard terminate for one still alive. No-op when none was launched.
void stop_launched_editors();

} // namespace mcp_test
