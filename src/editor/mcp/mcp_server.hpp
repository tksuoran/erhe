#pragma once

#include <atomic>
#include <chrono>
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
//   get_material_details, get_selection, get_shadow_fit_debug
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
    auto query_scene_textures   (const nlohmann::json& args) -> std::string;
    auto query_scene_brushes    (const nlohmann::json& args) -> std::string;
    auto query_selection        (const nlohmann::json& args) -> std::string;
    auto query_undo_redo_stack  (const nlohmann::json& args) -> std::string;
    auto query_async_status     (const nlohmann::json& args) -> std::string;
    auto query_shadow_fit_debug (const nlohmann::json& args) -> std::string;
    auto action_select_items    (const nlohmann::json& args) -> std::string;
    auto action_transform_selection(const nlohmann::json& args) -> std::string;
    auto action_place_brush     (const nlohmann::json& args) -> std::string;
    auto action_create_shape    (const nlohmann::json& args) -> std::string;
    auto action_create_node     (const nlohmann::json& args) -> std::string;
    auto action_toggle_physics  (const nlohmann::json& args) -> std::string;
    auto action_reparent_node   (const nlohmann::json& args) -> std::string;
    auto action_lock_items      (const nlohmann::json& args) -> std::string;
    auto action_unlock_items    (const nlohmann::json& args) -> std::string;
    auto action_add_tags        (const nlohmann::json& args) -> std::string;
    auto action_remove_tags     (const nlohmann::json& args) -> std::string;
    auto action_edit_material   (const nlohmann::json& args) -> std::string;
    auto action_save_scene      (const nlohmann::json& args) -> std::string;
    auto action_export_gltf     (const nlohmann::json& args) -> std::string;
    auto action_import_gltf     (const nlohmann::json& args) -> std::string;
    auto action_wake_physics_bodies(const nlohmann::json& args) -> std::string;
    auto query_physics_items    (const nlohmann::json& args) -> std::string;
    auto action_create_physics_body(const nlohmann::json& args) -> std::string;
    auto action_edit_physics_body  (const nlohmann::json& args) -> std::string;
    auto action_create_physics_joint(const nlohmann::json& args) -> std::string;
    auto action_edit_physics_joint  (const nlohmann::json& args) -> std::string;
    auto action_create_physics_material(const nlohmann::json& args) -> std::string;
    auto action_edit_physics_material  (const nlohmann::json& args) -> std::string;
    auto action_create_collision_filter(const nlohmann::json& args) -> std::string;
    auto action_edit_collision_filter  (const nlohmann::json& args) -> std::string;
    auto action_create_physics_joint_settings(const nlohmann::json& args) -> std::string;
    auto action_edit_physics_joint_settings  (const nlohmann::json& args) -> std::string;
    auto action_capture_screenshot           (const nlohmann::json& args) -> std::string;
    auto execute_command        (const std::string& tool_name) -> std::string;

    erhe::commands::Commands& m_commands;
    App_context&              m_context;
    int                       m_port;

    // Lifecycle (start/stop, server creation/destruction, thread join).
    // Serializes start() / stop() against each other and against
    // ~Mcp_server so the m_http_server / m_server_thread pair cannot
    // be observed in a half-torn-down state.
    std::mutex                       m_lifecycle_mutex;
    std::unique_ptr<httplib::Server> m_http_server;
    std::thread                      m_server_thread;
    std::atomic<bool>                m_running{false};

    // Bearer token loaded from $HOME/.claude/erhe_mcp_token (file
    // mode 0600 on POSIX). Empty when auth is disabled (the token
    // file is missing). When set, every /mcp request must present
    // Authorization: Bearer <m_auth_token>.
    std::string                      m_auth_token;

    // Maximum HTTP payload size (in bytes) accepted by httplib's POST
    // parser. Requests larger than this are rejected by httplib with
    // 413 before any handler runs.
    static constexpr std::size_t     k_max_payload_bytes = 1 * 1024 * 1024;

    // Maximum queue depth. Beyond this, handle_tools_call returns
    // JSON-RPC -32000 "server busy" instead of enqueuing.
    static constexpr std::size_t     k_max_queue_depth = 64;

    // How long a queued request may sit before process_queued_requests
    // drops it without mutating editor state. Must match the wait_for
    // in handle_tools_call so an abandoned promise can never reach
    // set_value (which would also leave the queue holding a
    // result_promise that nobody is listening on).
    static constexpr std::chrono::seconds k_request_timeout{5};

    // Tool info cache
    std::mutex                  m_tools_mutex;
    std::vector<Mcp_tool_info>  m_tool_infos;

    // Request queue (populated by HTTP thread, drained by main thread)
    struct Queued_request
    {
        std::string                                   tool_name;
        nlohmann::json                                arguments;
        std::promise<std::string>                     result_promise;
        std::chrono::steady_clock::time_point         enqueued_at{std::chrono::steady_clock::now()};
    };
    std::mutex                                       m_queue_mutex;
    std::vector<std::unique_ptr<Queued_request>>     m_request_queue;
};

} // namespace editor
