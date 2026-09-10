#include "editor_launcher.hpp"

#include <httplib.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#   define WIN32_LEAN_AND_MEAN
#   include <windows.h>
#else
#   include <csignal>
#   include <sys/types.h>
#   include <sys/wait.h>
#   include <unistd.h>
#endif

namespace mcp_test {

namespace {

// One editor started by launch_editor(), for stop_launched_editors().
class Launched_editor
{
public:
    std::string host;
    int         port{0};
    std::string token;
#if defined(_WIN32)
    HANDLE      process{nullptr};
#else
    pid_t       pid{0};
#endif
};

std::vector<Launched_editor> s_launched;

auto make_client(const std::string& host, const int port, const std::string& token) -> httplib::Client
{
    httplib::Client client{host, port};
    client.set_connection_timeout(2, 0);
    client.set_read_timeout      (10, 0);
    client.set_write_timeout     (10, 0);
    if (!token.empty()) {
        client.set_default_headers({{"Authorization", "Bearer " + token}});
    }
    return client;
}

auto health_ok(httplib::Client& client) -> bool
{
    httplib::Result res = client.Get("/health");
    return res && (res->status == 200);
}

auto wait_for_health(httplib::Client& client, const int timeout_seconds, const bool expect_alive) -> bool
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout_seconds};
    while (std::chrono::steady_clock::now() < deadline) {
        if (health_ok(client) == expect_alive) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }
    return false;
}

auto process_is_alive(const Launched_editor& editor) -> bool
{
#if defined(_WIN32)
    return (editor.process != nullptr) && (WaitForSingleObject(editor.process, 0) == WAIT_TIMEOUT);
#else
    if (editor.pid <= 0) {
        return false;
    }
    int status = 0;
    return waitpid(editor.pid, &status, WNOHANG) == 0;
#endif
}

void terminate_process(Launched_editor& editor)
{
#if defined(_WIN32)
    if (editor.process != nullptr) {
        if (process_is_alive(editor)) {
            TerminateProcess(editor.process, 1);
            WaitForSingleObject(editor.process, 10000);
        }
        CloseHandle(editor.process);
        editor.process = nullptr;
    }
#else
    if (editor.pid > 0) {
        if (process_is_alive(editor)) {
            kill(editor.pid, SIGKILL);
        }
        int status = 0;
        waitpid(editor.pid, &status, 0);
        editor.pid = 0;
    }
#endif
}

// Spawns the editor detached from this process (no inherited handles, own
// process group, no console window) with the given working directory and
// the MCP port (and token file, when not empty) in its environment. Fills
// the process handle of editor. Returns false with the reason on stderr.
auto spawn_editor(
    const std::string& editor_path,
    const std::string& working_directory,
    const int          port,
    const std::string& token_file,
    Launched_editor&   editor
) -> bool
{
    const std::string port_text = std::to_string(port);
#if defined(_WIN32)
    // The child receives a copy of this process's environment block.
    SetEnvironmentVariableA("ERHE_MCP_PORT",       port_text.c_str());
    SetEnvironmentVariableA("ERHE_AI_DRIVER",      "1");
    SetEnvironmentVariableA("ERHE_MCP_TOKEN_FILE", token_file.empty() ? nullptr : token_file.c_str());

    std::string command_line = "\"" + editor_path + "\"";
    STARTUPINFOA        startup_info{};
    PROCESS_INFORMATION process_info{};
    startup_info.cb = sizeof(startup_info);
    const BOOL created = CreateProcessA(
        editor_path.c_str(),
        command_line.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW,
        nullptr,
        working_directory.c_str(),
        &startup_info,
        &process_info
    );
    if (created == FALSE) {
        std::cerr << "mcp_server_tests: CreateProcess failed for " << editor_path
                  << " (error " << GetLastError() << ")\n";
        return false;
    }
    CloseHandle(process_info.hThread);
    editor.process = process_info.hProcess;
    return true;
#else
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "mcp_server_tests: fork failed for " << editor_path << "\n";
        return false;
    }
    if (pid == 0) {
        setsid();
        if (chdir(working_directory.c_str()) != 0) {
            _exit(127);
        }
        setenv("ERHE_MCP_PORT",  port_text.c_str(), 1);
        setenv("ERHE_AI_DRIVER", "1", 1);
        if (token_file.empty()) {
            unsetenv("ERHE_MCP_TOKEN_FILE");
        } else {
            setenv("ERHE_MCP_TOKEN_FILE", token_file.c_str(), 1);
        }
        execl(editor_path.c_str(), editor_path.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    editor.pid = pid;
    return true;
#endif
}

} // namespace

auto compiled_in_token_file() -> std::string
{
#if defined(ERHE_MCP_TEST_TOKEN_FILE)
    return ERHE_MCP_TEST_TOKEN_FILE;
#else
    return {};
#endif
}

auto read_token_file(const std::string& path) -> std::string
{
    if (path.empty()) {
        return {};
    }
    std::ifstream in{path};
    if (!in) {
        return {};
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string token = buffer.str();
    while (!token.empty() && ((token.back() == '\n') || (token.back() == '\r') || (token.back() == ' ') || (token.back() == '\t'))) {
        token.pop_back();
    }
    return token;
}

auto is_editor_reachable(const std::string& host, const int port) -> bool
{
    httplib::Client client = make_client(host, port, {});
    return health_ok(client);
}

auto launch_editor(
    const std::string& host,
    const int          port,
    const int          timeout_seconds,
    const std::string& token_file,
    const std::string& token
) -> bool
{
#if !defined(ERHE_MCP_TEST_EDITOR_PATH) || !defined(ERHE_MCP_TEST_REPO_ROOT)
    static_cast<void>(host);
    static_cast<void>(port);
    static_cast<void>(timeout_seconds);
    static_cast<void>(token_file);
    static_cast<void>(token);
    std::cerr << "mcp_server_tests: no editor path compiled in; start the editor first\n";
    return false;
#else
    for (const Launched_editor& editor : s_launched) {
        if ((editor.host == host) && (editor.port == port)) {
            std::cerr << "mcp_server_tests: an editor on " << host << ":" << port << " was already launched by this process\n";
            return false;
        }
    }
    const std::string editor_path       = ERHE_MCP_TEST_EDITOR_PATH;
    const std::string working_directory = ERHE_MCP_TEST_REPO_ROOT;
    std::cout << "mcp_server_tests: no MCP server at " << host << ":" << port
              << "; launching " << editor_path << " from " << working_directory
              << (token_file.empty() ? "" : " with token file " + token_file) << "\n";

    Launched_editor editor;
    editor.host  = host;
    editor.port  = port;
    editor.token = token;
    if (!spawn_editor(editor_path, working_directory, port, token_file, editor)) {
        return false;
    }

    httplib::Client client = make_client(host, port, {});
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{timeout_seconds};
    while (std::chrono::steady_clock::now() < deadline) {
        if (health_ok(client)) {
            std::cout << "mcp_server_tests: launched editor on port " << port << " is ready\n";
            s_launched.push_back(editor);
            return true;
        }
        if (!process_is_alive(editor)) {
            std::cerr << "mcp_server_tests: launched editor exited before its MCP server came up; see logs/log.txt\n";
            terminate_process(editor);
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }
    std::cerr << "mcp_server_tests: launched editor did not answer /health within " << timeout_seconds
              << " s (is port " << port << " free? grep logs/log.txt for 'MCP server: listening on')\n";
    terminate_process(editor);
    return false;
#endif
}

auto request_exit_and_wait(const std::string& host, const int port, const std::string& token) -> int
{
    httplib::Client client = make_client(host, port, token);
    if (!health_ok(client)) {
        std::cout << "mcp_server_tests: no MCP server at " << host << ":" << port << "; nothing to stop\n";
        return 0;
    }

    const std::string body =
        R"({"jsonrpc":"2.0","id":"fixture-stop","method":"tools/call","params":{"name":"request_exit","arguments":{}}})";
    httplib::Result res = client.Post("/mcp", body, "application/json");
    if (!res || (res->status != 200)) {
        std::cerr << "mcp_server_tests: request_exit call failed at " << host << ":" << port
                  << " (HTTP " << (res ? res->status : 0) << (token.empty() ? ", no token presented" : "") << ")\n";
        return 1;
    }

    if (wait_for_health(client, 60, false)) {
        std::cout << "mcp_server_tests: editor at " << host << ":" << port << " has exited\n";
        return 0;
    }
    std::cerr << "mcp_server_tests: editor at " << host << ":" << port << " still answering /health after 60 s\n";
    return 1;
}

void stop_launched_editors()
{
    for (Launched_editor& editor : s_launched) {
        request_exit_and_wait(editor.host, editor.port, editor.token);
        terminate_process(editor);
    }
    s_launched.clear();
}

} // namespace mcp_test
