#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace httplib {
    class Server;
}

namespace erhe::commands {
    class Command;
    class Commands;
}

namespace editor {

class App_context;

// Represents a single MCP tool descriptor
struct Mcp_tool_info
{
    std::string name;
    std::string description;
};

// MCP (Model Context Protocol) server that exposes editor commands over HTTP.
// Implements a subset of the MCP specification (JSON-RPC 2.0):
//   - initialize
//   - tools/list
//   - tools/call
//
// The server runs on a background thread and dispatches command invocations
// to a queue that is drained on the main editor thread each frame.
class Mcp_server
{
public:
    Mcp_server(
        erhe::commands::Commands& commands,
        App_context&              context,
        int                       port = 8080
    );
    ~Mcp_server() noexcept;

    Mcp_server(const Mcp_server&)            = delete;
    Mcp_server& operator=(const Mcp_server&) = delete;
    Mcp_server(Mcp_server&&)                 = delete;
    Mcp_server& operator=(Mcp_server&&)      = delete;

    // Start the HTTP server on its background thread.
    void start();

    // Stop the HTTP server and join its background thread.
    void stop();

    // Called once per frame from the main thread to execute any
    // queued command invocations. Returns the number of commands executed.
    auto process_queued_commands() -> int;

    [[nodiscard]] auto is_running() const -> bool;

private:
    void server_thread_main();
    void setup_routes();

    // JSON-RPC handlers
    auto handle_initialize  (const std::string& id) -> std::string;
    auto handle_tools_list  (const std::string& id) -> std::string;
    auto handle_tools_call  (const std::string& id, const std::string& tool_name) -> std::string;
    auto handle_error       (const std::string& id, int code, const std::string& message) -> std::string;

    // Build list of available MCP tools from registered commands
    void refresh_tool_list();

    erhe::commands::Commands& m_commands;
    App_context&              m_context;
    int                       m_port;

    std::unique_ptr<httplib::Server> m_http_server;
    std::thread                      m_server_thread;
    std::atomic<bool>                m_running{false};

    // Tool info cache
    std::mutex                  m_tools_mutex;
    std::vector<Mcp_tool_info> m_tool_infos;

    // Command execution queue (populated by HTTP thread, drained by main thread)
    struct Queued_command
    {
        std::string              tool_name;
        std::promise<bool>       result_promise;
    };
    std::mutex                              m_queue_mutex;
    std::vector<std::unique_ptr<Queued_command>> m_command_queue;
};

} // namespace editor
