#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace httplib {
    class Server;
}

namespace erhe::commands {
    class Command;
    class Commands;
}

namespace erhe {
    class Item_base;
}

namespace editor {

class App_context;
class Scene_root;

// Represents a single MCP tool descriptor
struct Mcp_tool_info
{
    std::string      name;
    std::string      description;
    nlohmann::json   input_schema;
};

// MCP (Model Context Protocol) server that exposes editor commands and
// scene/content-library queries over HTTP using JSON-RPC 2.0.
//
// Supported MCP methods:
//   - initialize
//   - tools/list
//   - tools/call
//
// Query tools: list_scenes, get_scene_nodes, get_node_details,
//   get_scene_cameras, get_scene_lights, get_scene_materials,
//   get_material_details, get_selection
//
// The server runs on a background thread and dispatches all requests
// to the main editor thread for thread safety.
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

    void start();
    void stop();

    // Called once per frame from the main thread.
    auto process_queued_requests() -> int;

    [[nodiscard]] auto is_running() const -> bool;

private:
    void server_thread_main();
    void setup_routes();

    // JSON-RPC handlers
    auto handle_initialize(const std::string& id) -> std::string;
    auto handle_tools_list(const std::string& id) -> std::string;
    auto handle_tools_call(const std::string& id, const std::string& tool_name, const nlohmann::json& arguments) -> std::string;
    auto handle_error     (const std::string& id, int code, const std::string& message) -> std::string;

    void refresh_tool_list();

    // Query handlers (run on main thread)
    auto find_scene             (const std::string& name) -> Scene_root*;
    auto find_items_by_ids      (Scene_root& sr, const std::set<std::size_t>& target_ids) -> std::vector<std::shared_ptr<erhe::Item_base>>;
    auto query_list_scenes      (const nlohmann::json& args) -> std::string;
    auto query_scene_nodes      (const nlohmann::json& args) -> std::string;
    auto query_node_details     (const nlohmann::json& args) -> std::string;
    auto query_scene_cameras    (const nlohmann::json& args) -> std::string;
    auto query_scene_lights     (const nlohmann::json& args) -> std::string;
    auto query_scene_materials  (const nlohmann::json& args) -> std::string;
    auto query_material_details (const nlohmann::json& args) -> std::string;
    auto query_scene_brushes    (const nlohmann::json& args) -> std::string;
    auto query_selection        (const nlohmann::json& args) -> std::string;
    auto action_select_items    (const nlohmann::json& args) -> std::string;
    auto action_place_brush     (const nlohmann::json& args) -> std::string;
    auto action_toggle_physics  (const nlohmann::json& args) -> std::string;
    auto action_lock_items      (const nlohmann::json& args) -> std::string;
    auto action_unlock_items    (const nlohmann::json& args) -> std::string;
    auto action_add_tags        (const nlohmann::json& args) -> std::string;
    auto action_remove_tags     (const nlohmann::json& args) -> std::string;
    auto execute_command        (const std::string& tool_name) -> std::string;

    erhe::commands::Commands& m_commands;
    App_context&              m_context;
    int                       m_port;

    std::unique_ptr<httplib::Server> m_http_server;
    std::thread                      m_server_thread;
    std::atomic<bool>                m_running{false};

    // Tool info cache
    std::mutex                  m_tools_mutex;
    std::vector<Mcp_tool_info>  m_tool_infos;

    // Request queue (populated by HTTP thread, drained by main thread)
    struct Queued_request
    {
        std::string                tool_name;
        nlohmann::json             arguments;
        std::promise<std::string>  result_promise;
    };
    std::mutex                                       m_queue_mutex;
    std::vector<std::unique_ptr<Queued_request>>     m_request_queue;
};

} // namespace editor
