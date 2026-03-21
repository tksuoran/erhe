#include "mcp/mcp_server.hpp"
#include "editor_log.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <future>

namespace editor {

using json = nlohmann::json;

namespace {

auto make_jsonrpc_response(const std::string& id, const json& result) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"result",  result}
    };
    return response.dump();
}

auto make_jsonrpc_error(const std::string& id, int code, const std::string& message) -> std::string
{
    json response = {
        {"jsonrpc", "2.0"},
        {"id",      id},
        {"error", {
            {"code",    code},
            {"message", message}
        }}
    };
    return response.dump();
}

} // anonymous namespace

Mcp_server::Mcp_server(
    erhe::commands::Commands& commands,
    App_context&              context,
    int                       port
)
    : m_commands{commands}
    , m_context {context}
    , m_port    {port}
{
}

Mcp_server::~Mcp_server() noexcept
{
    stop();
}

void Mcp_server::start()
{
    if (m_running.load()) {
        return;
    }

    log_mcp->info("MCP server: starting on port {}", m_port);

    refresh_tool_list();

    m_http_server = std::make_unique<httplib::Server>();
    setup_routes();

    m_running.store(true);
    m_server_thread = std::thread{&Mcp_server::server_thread_main, this};
}

void Mcp_server::stop()
{
    if (!m_running.load()) {
        return;
    }

    log_mcp->info("MCP server: stopping");

    m_running.store(false);
    if (m_http_server) {
        m_http_server->stop();
    }
    if (m_server_thread.joinable()) {
        m_server_thread.join();
    }
    m_http_server.reset();

    log_mcp->info("MCP server: stopped");
}

auto Mcp_server::is_running() const -> bool
{
    return m_running.load();
}

void Mcp_server::server_thread_main()
{
    log_mcp->info("MCP server: listening on 127.0.0.1:{}", m_port);

    if (!m_http_server->listen("127.0.0.1", m_port)) {
        if (m_running.load()) {
            log_mcp->error("MCP server: failed to listen on port {}", m_port);
        }
    }

    m_running.store(false);
    log_mcp->info("MCP server: thread exiting");
}

void Mcp_server::setup_routes()
{
    // MCP uses JSON-RPC 2.0 over HTTP POST
    m_http_server->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error&) {
            res.body = make_jsonrpc_error("null", -32700, "Parse error");
            return;
        }

        const std::string id     = request.value("id", "null");
        const std::string method = request.value("method", "");

        if (method == "initialize") {
            res.body = handle_initialize(id);
        } else if (method == "tools/list") {
            res.body = handle_tools_list(id);
        } else if (method == "tools/call") {
            const auto& params = request.value("params", json::object());
            const std::string tool_name = params.value("name", "");
            if (tool_name.empty()) {
                res.body = make_jsonrpc_error(id, -32602, "Missing tool name in params.name");
            } else {
                res.body = handle_tools_call(id, tool_name);
            }
        } else {
            res.body = make_jsonrpc_error(id, -32601, "Method not found: " + method);
        }
    });

    // Health check endpoint
    m_http_server->Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");
        res.body = R"({"status":"ok"})";
    });
}

auto Mcp_server::handle_initialize(const std::string& id) -> std::string
{
    json result = {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", json::object()}
        }},
        {"serverInfo", {
            {"name",    "erhe-editor"},
            {"version", "0.1.0"}
        }}
    };
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_list(const std::string& id) -> std::string
{
    // Refresh the tool list each time in case commands changed
    refresh_tool_list();

    json tools = json::array();
    {
        std::lock_guard<std::mutex> lock{m_tools_mutex};
        for (const auto& tool : m_tool_infos) {
            json tool_json = {
                {"name",        tool.name},
                {"description", tool.description},
                {"inputSchema", {
                    {"type",       "object"},
                    {"properties", json::object()}
                }}
            };
            tools.push_back(tool_json);
        }
    }

    json result = {
        {"tools", tools}
    };
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_call(const std::string& id, const std::string& tool_name) -> std::string
{
    // Queue the command for main-thread execution and wait for result
    auto queued = std::make_unique<Queued_command>();
    queued->tool_name = tool_name;
    std::future<bool> result_future = queued->result_promise.get_future();

    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        m_command_queue.push_back(std::move(queued));
    }

    // Wait for the main thread to process this command (with timeout)
    const auto status = result_future.wait_for(std::chrono::seconds{5});
    if (status == std::future_status::timeout) {
        return make_jsonrpc_error(id, -32000, "Command execution timed out: " + tool_name);
    }

    const bool success = result_future.get();
    if (success) {
        json result = {
            {"content", {{
                {"type", "text"},
                {"text", "Command executed successfully: " + tool_name}
            }}}
        };
        return make_jsonrpc_response(id, result);
    } else {
        json result = {
            {"content", {{
                {"type", "text"},
                {"text", "Command failed or not found: " + tool_name}
            }}},
            {"isError", true}
        };
        return make_jsonrpc_response(id, result);
    }
}

auto Mcp_server::handle_error(const std::string& id, int code, const std::string& message) -> std::string
{
    return make_jsonrpc_error(id, code, message);
}

auto Mcp_server::process_queued_commands() -> int
{
    std::vector<std::unique_ptr<Queued_command>> commands_to_execute;
    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        commands_to_execute.swap(m_command_queue);
    }

    int executed_count = 0;
    for (auto& queued : commands_to_execute) {
        bool found = false;
        const auto& registered_commands = m_commands.get_commands();
        for (auto* command : registered_commands) {
            if (queued->tool_name == command->get_name()) {
                const bool result = command->try_call();
                queued->result_promise.set_value(result);
                found = true;
                ++executed_count;
                log_mcp->info("MCP server: executed command '{}'  result={}", queued->tool_name, result);
                break;
            }
        }
        if (!found) {
            queued->result_promise.set_value(false);
            log_mcp->warn("MCP server: command not found: '{}'", queued->tool_name);
        }
    }

    return executed_count;
}

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }

        Mcp_tool_info info;
        info.name        = name;
        info.description = std::string{"Editor command: "} + name;
        m_tool_infos.push_back(std::move(info));
    }
}

} // namespace editor
