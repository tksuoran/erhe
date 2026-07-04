// Mcp_server lifecycle, HTTP routing, JSON-RPC handling and tool dispatch.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

Mcp_server::Mcp_server(
    erhe::commands::Commands& commands,
    App_context&              context,
    int                       port
)
    : m_commands{commands}
    , m_context {context}
    , m_port    {port}
{
    m_auth_token = load_auth_token();
    if (m_auth_token.empty()) {
        log_mcp->warn(
            "MCP server: no bearer token loaded (write a secret to ~/.claude/erhe_mcp_token "
            "with mode 0600 to require Authorization: Bearer)"
        );
    } else {
        log_mcp->info("MCP server: bearer-token auth enabled");
    }
}

Mcp_server::~Mcp_server() noexcept
{
    // ~thread on a still-joinable handle calls std::terminate; the
    // join itself can throw (system_error on EDEADLK / EINVAL).
    // Catch and log so destruction is noexcept in practice and any
    // platform-level join failure leaves a breadcrumb instead of
    // crashing the editor at shutdown.
    try {
        stop();
    } catch (const std::system_error& e) {
        log_mcp->error("MCP server: ~Mcp_server caught system_error during stop: {}", e.what());
    } catch (const std::exception& e) {
        log_mcp->error("MCP server: ~Mcp_server caught exception during stop: {}", e.what());
    } catch (...) {
        log_mcp->error("MCP server: ~Mcp_server caught unknown exception during stop");
    }
}

void Mcp_server::start()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    if (m_running.load() || m_server_thread.joinable() || m_http_server) {
        return;
    }

    log_mcp->info("MCP server: starting on port {}", m_port);

    refresh_tool_list();

    // Bind the preferred port, falling back through up to k_port_retry_count-1
    // successors (e.g. 8080..8099) when it is already in use. This matters on
    // Quest, where another service may already hold 8080; without the fallback
    // the editor came up with no reachable MCP endpoint. Each FAILED
    // httplib::Server::bind_to_port() permanently decommissions that Server
    // instance (it sets is_decommissioned, after which every later bind on the
    // same object returns -1 regardless of port), so each attempt must use a
    // FRESH Server. Binding is non-blocking, so it runs here on the caller
    // thread; only the blocking accept loop (listen_after_bind) runs on
    // m_server_thread.
    constexpr int k_port_retry_count = 20;
    const int     preferred_port     = m_port;
    int           bound_port         = -1;
    for (int candidate = preferred_port; candidate < (preferred_port + k_port_retry_count); ++candidate) {
        m_http_server = std::make_unique<httplib::Server>();
        m_http_server->set_payload_max_length(k_max_payload_bytes);
        setup_routes();
        if (m_http_server->bind_to_port("127.0.0.1", candidate)) {
            bound_port = candidate;
            break;
        }
        m_http_server.reset(); // decommissioned by the failed bind; retry with a fresh Server
    }

    if (bound_port < 0) {
        log_mcp->error(
            "MCP server: failed to bind any port in [{}, {}) - all in use?",
            preferred_port, preferred_port + k_port_retry_count
        );
        return;
    }

    m_port = bound_port;
    if (bound_port != preferred_port) {
        log_mcp->warn("MCP server: port {} unavailable; bound to {} instead", preferred_port, bound_port);
    }

    m_running.store(true);
    m_server_thread = std::thread{&Mcp_server::server_thread_main, this};
}

void Mcp_server::stop()
{
    std::lock_guard<std::mutex> lock{m_lifecycle_mutex};

    // Worker thread flips m_running to false on its way out of
    // httplib::listen() (server_thread_main), so checking m_running
    // alone would miss the post-listen window where the thread is
    // still joinable. Check both the thread and the server pointer.
    if (!m_server_thread.joinable() && !m_http_server) {
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
    // m_http_server is already bound to m_port by start(); enter the blocking
    // accept loop. stop() unblocks this via m_http_server->stop().
    log_mcp->info("MCP server: listening on 127.0.0.1:{} (pid {}, built {})", m_port, get_process_id(), c_mcp_build_timestamp);

    if (!m_http_server->listen_after_bind()) {
        if (m_running.load()) {
            log_mcp->error("MCP server: listen_after_bind() failed on port {}", m_port);
        }
    }

    m_running.store(false);
    log_mcp->info("MCP server: thread exiting");
}

void Mcp_server::setup_routes()
{
    m_http_server->Post("/mcp", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Content-Type", "application/json");

        // Auth check first. When m_auth_token is empty (token file not
        // present or unreadable) auth is disabled and any request is
        // accepted; the operator gets a startup warning so disabled
        // auth is loud rather than silent.
        if (!m_auth_token.empty()) {
            const std::optional<std::string> presented = bearer_token_from(req);
            if (!presented.has_value() || !constant_time_equal(*presented, m_auth_token)) {
                res.status = 401;
                res.set_header("WWW-Authenticate", "Bearer realm=\"erhe-mcp\"");
                res.body = make_jsonrpc_error("null", -32001, "Unauthorized");
                return;
            }
        }

        json request;
        try {
            request = json::parse(req.body);
        } catch (const json::parse_error&) {
            res.body = make_jsonrpc_error("null", -32700, "Parse error");
            return;
        }

        // JSON-RPC allows the id to be a string, a number, or null. json::value
        // with a string default throws json::type_error on a numeric id, which
        // httplib turns into an opaque HTTP 500. Normalize any id type to the
        // string form used internally for the response echo.
        std::string id = "null";
        if (request.contains("id")) {
            const json& id_json = request.at("id");
            if (id_json.is_string()) {
                id = id_json.get<std::string>();
            } else if (id_json.is_number_integer()) {
                id = std::to_string(id_json.get<int64_t>());
            } else if (id_json.is_number_unsigned()) {
                id = std::to_string(id_json.get<uint64_t>());
            } else if (id_json.is_number_float()) {
                id = std::to_string(id_json.get<double>());
            }
        }
        const std::string method = request.value("method", "");

        if (method == "initialize") {
            res.body = handle_initialize(id);
        } else if (method == "tools/list") {
            res.body = handle_tools_list(id);
        } else if (method == "tools/call") {
            const auto& params    = request.value("params", json::object());
            const std::string tool_name = params.value("name", "");
            const json arguments = params.value("arguments", json::object());
            if (tool_name.empty()) {
                res.body = make_jsonrpc_error(id, -32602, "Missing tool name in params.name");
            } else {
                res.body = handle_tools_call(id, tool_name, arguments);
            }
        } else {
            res.body = make_jsonrpc_error(id, -32601, "Method not found: " + method);
        }
    });

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
            {"version", "0.2.0"},
            {"pid",     get_process_id()},
            {"build",   c_mcp_build_timestamp}
        }}
    };
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_list(const std::string& id) -> std::string
{
    refresh_tool_list();

    json tools = json::array();
    {
        std::lock_guard<std::mutex> lock{m_tools_mutex};
        for (const auto& tool : m_tool_infos) {
            json tool_json = {
                {"name",        tool.name},
                {"description", tool.description},
                {"inputSchema", tool.input_schema}
            };
            tools.push_back(tool_json);
        }
    }

    json result = {{"tools", tools}};
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_tools_call(
    const std::string& id,
    const std::string& tool_name,
    const json&        arguments
) -> std::string
{
    auto queued = std::make_unique<Queued_request>();
    queued->tool_name = tool_name;
    queued->arguments = arguments;
    std::future<std::string> result_future = queued->result_promise.get_future();

    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        if (m_request_queue.size() >= k_max_queue_depth) {
            // Drop without enqueuing so process_queued_requests cannot
            // pop a stale entry whose HTTP client has already timed
            // out. -32000 is "server error" in JSON-RPC's reserved
            // range; the message is what differentiates it from other
            // -32000s.
            return make_jsonrpc_error(id, -32000, "Server busy: request queue full");
        }
        m_request_queue.push_back(std::move(queued));
    }

    const auto status = result_future.wait_for(k_request_timeout);
    if (status == std::future_status::timeout) {
        return make_jsonrpc_error(id, -32000, "Request timed out: " + tool_name);
    }

    std::string result_json;
    try {
        result_json = result_future.get();
    } catch (const std::future_error& e) {
        // The queued request's promise was destroyed without being
        // settled. This can happen if Mcp_server is being torn down
        // while HTTP handler threads are still waiting (the queue
        // unique_ptrs are freed before this thread observes ready).
        // Translate the broken_promise into a clean JSON-RPC error
        // so the exception cannot escape into httplib's dispatcher.
        log_mcp->warn("MCP server: future_error in handle_tools_call: {}", e.what());
        return make_jsonrpc_error(id, -32000, "Internal: request abandoned: " + tool_name);
    }
    json result = json::parse(result_json, nullptr, false);
    if (result.is_discarded()) {
        return make_jsonrpc_error(id, -32000, "Internal error processing: " + tool_name);
    }
    return make_jsonrpc_response(id, result);
}

auto Mcp_server::handle_error(const std::string& id, int code, const std::string& message) -> std::string
{
    return make_jsonrpc_error(id, code, message);
}

auto Mcp_server::process_queued_requests() -> int
{
    std::vector<std::unique_ptr<Queued_request>> requests;
    {
        std::lock_guard<std::mutex> lock{m_queue_mutex};
        requests.swap(m_request_queue);
    }

    const auto now = std::chrono::steady_clock::now();
    int count = 0;
    for (auto& req : requests) {
        // Drop entries whose HTTP client has already given up (wait_for
        // returned timeout in handle_tools_call). Without this guard
        // a slow editor frame would still mutate editor state for a
        // request the operator already considers failed. We still
        // settle the promise so the future destructor does not abort.
        if ((now - req->enqueued_at) >= k_request_timeout) {
            req->result_promise.set_value(
                make_jsonrpc_error("dropped", -32000, "Request expired before processing: " + req->tool_name)
            );
            log_mcp->warn("MCP server: dropped expired '{}' before processing", req->tool_name);
            continue;
        }

        // Per-request exception boundary. process_queued_requests() runs on the
        // main thread, so a handler that throws would skip the set_value() below,
        // break the waiting HTTP thread's promise (observed as
        // future_error "broken promise"), and - fatally - escape up the main
        // thread into the crash handler, taking down the whole editor. A single
        // bad tool call must instead become a JSON-RPC tool error. The throw is
        // logged loudly so the offending handler can still be tracked down.
        std::string result;
        try {

        if      (req->tool_name == "list_scenes")         result = query_list_scenes     (req->arguments);
        else if (req->tool_name == "get_scene_nodes")     result = query_scene_nodes     (req->arguments);
        else if (req->tool_name == "get_node_details")    result = query_node_details    (req->arguments);
        else if (req->tool_name == "get_scene_cameras")   result = query_scene_cameras   (req->arguments);
        else if (req->tool_name == "get_scene_lights")    result = query_scene_lights    (req->arguments);
        else if (req->tool_name == "get_scene_materials") result = query_scene_materials (req->arguments);
        else if (req->tool_name == "get_scene_textures") result = query_scene_textures  (req->arguments);
        else if (req->tool_name == "get_scene_brushes")  result = query_scene_brushes  (req->arguments);
        else if (req->tool_name == "get_material_details")result = query_material_details(req->arguments);
        else if (req->tool_name == "get_server_info")     result = query_server_info     (req->arguments);
        else if (req->tool_name == "get_selection")       result = query_selection       (req->arguments);
        else if (req->tool_name == "get_undo_redo_stack") result = query_undo_redo_stack (req->arguments);
        else if (req->tool_name == "get_async_status")   result = query_async_status    (req->arguments);
        else if (req->tool_name == "get_shadow_fit_debug")result = query_shadow_fit_debug(req->arguments);
        else if (req->tool_name == "select_items")       result = action_select_items   (req->arguments);
        else if (req->tool_name == "transform_selection") result = action_transform_selection(req->arguments);
        else if (req->tool_name == "place_brush")        result = action_place_brush    (req->arguments);
        else if (req->tool_name == "create_shape")       result = action_create_shape   (req->arguments);
        else if (req->tool_name == "create_node")        result = action_create_node    (req->arguments);
        else if (req->tool_name == "create_light")       result = action_create_light   (req->arguments);
        else if (req->tool_name == "edit_light")         result = action_edit_light     (req->arguments);
        else if (req->tool_name == "edit_camera")        result = action_edit_camera    (req->arguments);
        else if (req->tool_name == "toggle_physics")     result = action_toggle_physics (req->arguments);
        else if (req->tool_name == "reparent_node")      result = action_reparent_node  (req->arguments);
        else if (req->tool_name == "lock_items")         result = action_lock_items     (req->arguments);
        else if (req->tool_name == "unlock_items")       result = action_unlock_items   (req->arguments);
        else if (req->tool_name == "add_tags")           result = action_add_tags       (req->arguments);
        else if (req->tool_name == "remove_tags")        result = action_remove_tags    (req->arguments);
        else if (req->tool_name == "edit_material")      result = action_edit_material  (req->arguments);
        else if (req->tool_name == "save_scene")         result = action_save_scene     (req->arguments);
        else if (req->tool_name == "load_scene")         result = action_load_scene     (req->arguments);
        else if (req->tool_name == "close_scene")        result = action_close_scene    (req->arguments);
        else if (req->tool_name == "export_gltf")        result = action_export_gltf    (req->arguments);
        else if (req->tool_name == "import_gltf")        result = action_import_gltf    (req->arguments);
        else if (req->tool_name == "capture_screenshot") result = action_capture_screenshot(req->arguments);
        else if (req->tool_name == "wake_physics_bodies") result = action_wake_physics_bodies(req->arguments);
        else if (req->tool_name == "get_physics_items")  result = query_physics_items   (req->arguments);
        else if (req->tool_name == "get_physics_state")  result = query_get_physics_state(req->arguments);
        else if (req->tool_name == "create_physics_body") result = action_create_physics_body(req->arguments);
        else if (req->tool_name == "edit_physics_body")  result = action_edit_physics_body(req->arguments);
        else if (req->tool_name == "create_physics_joint") result = action_create_physics_joint(req->arguments);
        else if (req->tool_name == "edit_physics_joint") result = action_edit_physics_joint(req->arguments);
        else if (req->tool_name == "create_physics_material") result = action_create_physics_material(req->arguments);
        else if (req->tool_name == "edit_physics_material")   result = action_edit_physics_material(req->arguments);
        else if (req->tool_name == "create_collision_filter") result = action_create_collision_filter(req->arguments);
        else if (req->tool_name == "edit_collision_filter")   result = action_edit_collision_filter(req->arguments);
        else if (req->tool_name == "create_physics_joint_settings") result = action_create_physics_joint_settings(req->arguments);
        else if (req->tool_name == "edit_physics_joint_settings")   result = action_edit_physics_joint_settings(req->arguments);
        else if (req->tool_name == "set_mesh_component_mode")        result = action_set_mesh_component_mode(req->arguments);
        else if (req->tool_name == "select_mesh_components")         result = action_select_mesh_components(req->arguments);
        else if (req->tool_name == "grow_mesh_selection")           result = action_grow_mesh_selection(req->arguments);
        else if (req->tool_name == "shrink_mesh_selection")         result = action_shrink_mesh_selection(req->arguments);
        else if (req->tool_name == "get_mesh_component_selection")   result = query_mesh_component_selection(req->arguments);
        else if (req->tool_name == "get_id_range_mapping")           result = query_id_range_mapping(req->arguments);
        else if (req->tool_name == "debug_region_select")            result = action_debug_region_select(req->arguments);
        else if (req->tool_name == "get_mesh_geometry_info")         result = query_mesh_geometry_info(req->arguments);
        else if (req->tool_name == "get_mesh_attribute_values")      result = query_mesh_attribute_values(req->arguments);
        else if (req->tool_name == "clear_mesh_component_selection") result = action_clear_mesh_component_selection(req->arguments);
        else if (req->tool_name == "set_edge_sharpness")             result = action_set_edge_sharpness(req->arguments);
        else if (req->tool_name == "catmull_clark")                  result = action_catmull_clark(req->arguments);
        else if (req->tool_name == "align_components")              result = action_align_components(req->arguments);
        else if (req->tool_name == "add_joint")                    result = action_add_joint(req->arguments);
        else if (req->tool_name == "flip_joint")                   result = action_flip_joint(req->arguments);
        else if (req->tool_name == "remesh")                       result = action_remesh(req->arguments);
        else if (req->tool_name == "decimate")                     result = action_decimate(req->arguments);
        else if (req->tool_name == "smooth")                       result = action_smooth(req->arguments);
        else if (req->tool_name == "chamfer")                      result = action_chamfer3(req->arguments);
        else if (req->tool_name == "merge_faces")                  result = action_merge_faces(req->arguments);
        else if (req->tool_name == "generate_texture_coordinates") result = action_generate_texture_coordinates(req->arguments);
        else if (req->tool_name == "set_transform_reference_mode") result = action_set_transform_reference_mode(req->arguments);
        else if (req->tool_name == "set_transform_mode")           result = action_set_transform_mode          (req->arguments);
        else if (req->tool_name == "get_transform_state")          result = query_transform_state              (req->arguments);
        else if (req->tool_name == "get_geometry_graph")           result = query_geometry_graph               (req->arguments);
        else if (req->tool_name == "geometry_graph_add_node")      result = action_geometry_graph_add_node     (req->arguments);
        else if (req->tool_name == "geometry_graph_remove_node")   result = action_geometry_graph_remove_node  (req->arguments);
        else if (req->tool_name == "geometry_graph_set_parameter") result = action_geometry_graph_set_parameter(req->arguments);
        else if (req->tool_name == "geometry_graph_connect")       result = action_geometry_graph_connect      (req->arguments);
        else if (req->tool_name == "geometry_graph_disconnect")    result = action_geometry_graph_disconnect   (req->arguments);
        else if (req->tool_name == "create_graph_texture")         result = action_create_graph_texture        (req->arguments);
        else if (req->tool_name == "set_material_texture_source")  result = action_set_material_texture_source (req->arguments);
        else if (req->tool_name == "get_graph_textures")           result = query_graph_textures               (req->arguments);
        else if (req->tool_name == "create_graph_mesh")            result = action_create_graph_mesh           (req->arguments);
        else if (req->tool_name == "set_node_graph_mesh")          result = action_set_node_graph_mesh         (req->arguments);
        else if (req->tool_name == "get_graph_meshes")             result = query_graph_meshes                 (req->arguments);
        else if (req->tool_name == "get_texture_graph")            result = query_texture_graph                (req->arguments);
        else if (req->tool_name == "texture_graph_add_node")       result = action_texture_graph_add_node      (req->arguments);
        else if (req->tool_name == "texture_graph_remove_node")    result = action_texture_graph_remove_node   (req->arguments);
        else if (req->tool_name == "texture_graph_set_parameter")  result = action_texture_graph_set_parameter (req->arguments);
        else if (req->tool_name == "texture_graph_connect")        result = action_texture_graph_connect       (req->arguments);
        else if (req->tool_name == "texture_graph_disconnect")     result = action_texture_graph_disconnect    (req->arguments);
        else if (req->tool_name == "texture_graph_export_png")     result = action_texture_graph_export_png    (req->arguments);
        else if (req->tool_name == "texture_graph_export_material") result = action_texture_graph_export_material(req->arguments);
        else                                              result = execute_command       (req->tool_name);

        } catch (const std::exception& e) {
            log_mcp->error("MCP server: handler for '{}' threw: {}", req->tool_name, e.what());
            result = make_error_content(std::string{"Handler '"} + req->tool_name + "' threw an exception: " + e.what());
        } catch (...) {
            log_mcp->error("MCP server: handler for '{}' threw a non-standard exception", req->tool_name);
            result = make_error_content(std::string{"Handler '"} + req->tool_name + "' threw a non-standard exception");
        }

        req->result_promise.set_value(std::move(result));
        ++count;
        log_mcp->info("MCP server: processed '{}'", req->tool_name);
    }

    return count;
}

auto Mcp_server::find_scene(const std::string& name) -> Scene_root*
{
    if (!m_context.app_scenes) {
        return nullptr;
    }
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        if (sr->get_name() == name) {
            return sr.get();
        }
    }
    return nullptr;
}

auto Mcp_server::execute_command(const std::string& tool_name) -> std::string
{
    const auto& registered_commands = m_commands.get_commands();
    for (auto* command : registered_commands) {
        if (tool_name == command->get_name()) {
            const bool success = command->try_call();
            if (success) {
                return make_text_content("Command executed successfully: " + tool_name).dump();
            } else {
                json r = make_text_content("Command failed: " + tool_name);
                r["isError"] = true;
                return r.dump();
            }
        }
    }
    json r = make_text_content("Command not found: " + tool_name);
    r["isError"] = true;
    return r.dump();
}


} // namespace editor
