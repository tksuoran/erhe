#include "mcp/mcp_server.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "brushes/brush.hpp"
#include "brushes/brush_placement.hpp"
#include "content_library/content_library.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "erhe_math/math_util.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_commands/commands.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/trs_transform.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <set>

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

auto make_text_content(const std::string& text) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", text}
        }}}
    };
}

auto make_json_content(const json& data) -> json
{
    return {
        {"content", {{
            {"type", "text"},
            {"text", data.dump(2)}
        }}}
    };
}

auto schema_no_args() -> json
{
    return {{"type", "object"}, {"properties", json::object()}};
}

auto schema_scene_name() -> json
{
    return {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}
        }},
        {"required", json::array({"scene_name"})}
    };
}

auto schema_scene_and_item(const char* item_key, const char* item_desc) -> json
{
    return {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {item_key,     {{"type", "string"}, {"description", item_desc}}}
        }},
        {"required", json::array({"scene_name", item_key})}
    };
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
            {"version", "0.2.0"}
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
        m_request_queue.push_back(std::move(queued));
    }

    const auto status = result_future.wait_for(std::chrono::seconds{5});
    if (status == std::future_status::timeout) {
        return make_jsonrpc_error(id, -32000, "Request timed out: " + tool_name);
    }

    const std::string result_json = result_future.get();
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

    int count = 0;
    for (auto& req : requests) {
        std::string result;

        if      (req->tool_name == "list_scenes")         result = query_list_scenes     (req->arguments);
        else if (req->tool_name == "get_scene_nodes")     result = query_scene_nodes     (req->arguments);
        else if (req->tool_name == "get_node_details")    result = query_node_details    (req->arguments);
        else if (req->tool_name == "get_scene_cameras")   result = query_scene_cameras   (req->arguments);
        else if (req->tool_name == "get_scene_lights")    result = query_scene_lights    (req->arguments);
        else if (req->tool_name == "get_scene_materials") result = query_scene_materials (req->arguments);
        else if (req->tool_name == "get_scene_brushes")  result = query_scene_brushes  (req->arguments);
        else if (req->tool_name == "get_material_details")result = query_material_details(req->arguments);
        else if (req->tool_name == "get_selection")       result = query_selection       (req->arguments);
        else if (req->tool_name == "get_undo_redo_stack") result = query_undo_redo_stack (req->arguments);
        else if (req->tool_name == "get_async_status")   result = query_async_status    (req->arguments);
        else if (req->tool_name == "select_items")       result = action_select_items   (req->arguments);
        else if (req->tool_name == "place_brush")        result = action_place_brush    (req->arguments);
        else if (req->tool_name == "toggle_physics")     result = action_toggle_physics (req->arguments);
        else if (req->tool_name == "lock_items")         result = action_lock_items     (req->arguments);
        else if (req->tool_name == "unlock_items")       result = action_unlock_items   (req->arguments);
        else if (req->tool_name == "add_tags")           result = action_add_tags       (req->arguments);
        else if (req->tool_name == "remove_tags")        result = action_remove_tags    (req->arguments);
        else                                              result = execute_command       (req->tool_name);

        req->result_promise.set_value(std::move(result));
        ++count;
        log_mcp->info("MCP server: processed '{}'", req->tool_name);
    }

    return count;
}

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    // Query tools
    m_tool_infos.push_back({"list_scenes",         "List all scenes in the editor",                          schema_no_args()});
    m_tool_infos.push_back({"get_scene_nodes",     "List all nodes in a scene",                              schema_scene_name()});
    m_tool_infos.push_back({"get_node_details",    "Get detailed info for a node (transform, attachments)",  schema_scene_and_item("node_name", "Name of the node")});
    m_tool_infos.push_back({"get_scene_cameras",   "List all cameras in a scene",                            schema_scene_name()});
    m_tool_infos.push_back({"get_scene_lights",    "List all lights in a scene",                             schema_scene_name()});
    m_tool_infos.push_back({"get_scene_materials", "List all materials in a scene's content library",        schema_scene_name()});
    m_tool_infos.push_back({"get_material_details","Get detailed material properties",                       schema_scene_and_item("material_name", "Name of the material")});
    m_tool_infos.push_back({"get_scene_brushes",  "List all brushes in a scene's content library",         schema_scene_name()});
    m_tool_infos.push_back({"get_selection",        "Get currently selected items",                          schema_no_args()});
    m_tool_infos.push_back({"get_undo_redo_stack", "Get undo/redo operation stacks",                       schema_no_args()});
    m_tool_infos.push_back({"get_async_status",   "Get pending/running async operation counts",          schema_no_args()});
    m_tool_infos.push_back({"select_items",        "Select items by ID (scene nodes, materials, etc.)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to search for items"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Array of item IDs to select"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"place_brush",         "Place a brush instance in a scene at a given position", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"brush_id",      {{"type", "integer"}, {"description", "Brush ID (from get_scene_brushes)"}}},
            {"position",      {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "World position [x, y, z]"}}},
            {"material_name", {{"type", "string"},  {"description", "Material name (optional, uses first available if omitted)"}}},
            {"scale",         {{"type", "number"},  {"description", "Scale factor (optional, default 1.0)"}}},
            {"motion_mode",   {{"type", "string"},  {"description", "Physics mode: static, dynamic (optional, default dynamic)"}}}
        }},
        {"required", json::array({"scene_name", "brush_id", "position"})}
    }});

    m_tool_infos.push_back({"toggle_physics",     "Toggle dynamic physics simulation on/off",              schema_no_args()});
    m_tool_infos.push_back({"lock_items",         "Lock items by ID (prevents deletion/modification)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to lock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"unlock_items",       "Unlock items by ID",                                  {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to unlock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"add_tags",           "Add tags to items by ID",                             {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to tag"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to add"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    m_tool_infos.push_back({"remove_tags",        "Remove tags from items by ID",                        {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to remove"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});

    // Editor commands
    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        m_tool_infos.push_back({name, std::string{"Editor command: "} + name, schema_no_args()});
    }
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

// --- Query implementations ---

auto Mcp_server::query_list_scenes(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_scenes) {
        return make_text_content("No scenes available").dump();
    }

    json scenes = json::array();
    for (const auto& sr : m_context.app_scenes->get_scene_roots()) {
        const auto& scene = sr->get_scene();
        const auto  library = sr->get_content_library();

        int material_count = 0;
        if (library && library->materials) {
            material_count = static_cast<int>(library->materials->get_all<erhe::primitive::Material>().size());
        }

        int light_count = 0;
        for (const auto& ll : scene.get_light_layers()) {
            light_count += static_cast<int>(ll->lights.size());
        }

        scenes.push_back({
            {"name",           sr->get_name()},
            {"node_count",     static_cast<int>(scene.get_flat_nodes().size())},
            {"camera_count",   static_cast<int>(scene.get_cameras().size())},
            {"light_count",    light_count},
            {"material_count", material_count}
        });
    }

    return make_json_content({{"scenes", scenes}}).dump();
}

auto Mcp_server::query_scene_nodes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    json nodes = json::array();
    for (const auto& node : scene.get_flat_nodes()) {
        const auto& trs = node->parent_from_node_transform();
        const glm::vec3 t = trs.get_translation();
        const glm::quat r = trs.get_rotation();
        const glm::vec3 s = trs.get_scale();

        json attachment_types = json::array();
        for (const auto& att : node->get_attachments()) {
            attachment_types.push_back(std::string{att->get_type_name()});
        }

        auto parent_node = node->get_parent_node();

        json tags_arr = json::array();
        for (const auto& tag : node->get_tags()) {
            tags_arr.push_back(tag);
        }

        nodes.push_back({
            {"name",             node->get_name()},
            {"id",               node->get_id()},
            {"parent",           parent_node ? parent_node->get_name() : ""},
            {"position",         {t.x, t.y, t.z}},
            {"rotation_xyzw",    {r.x, r.y, r.z, r.w}},
            {"scale",            {s.x, s.y, s.z}},
            {"attachment_types", attachment_types},
            {"locked",           node->is_lock_edit()},
            {"tags",             tags_arr}
        });
    }

    return make_json_content({{"nodes", nodes}}).dump();
}

auto Mcp_server::query_node_details(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    const std::string node_name  = args.value("node_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& scene = sr->get_scene();
    std::shared_ptr<erhe::scene::Node> found_node;
    for (const auto& node : scene.get_flat_nodes()) {
        if (node->get_name() == node_name) {
            found_node = node;
            break;
        }
    }
    if (!found_node) {
        json r = make_text_content("Node not found: " + node_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& trs = found_node->parent_from_node_transform();
    const glm::vec3 t = trs.get_translation();
    const glm::quat r = trs.get_rotation();
    const glm::vec3 s = trs.get_scale();
    const glm::vec4 wp = found_node->position_in_world();

    json attachments = json::array();
    for (const auto& att : found_node->get_attachments()) {
        json att_json = {
            {"type", std::string{att->get_type_name()}},
            {"name", att->get_name()}
        };

        auto mesh = std::dynamic_pointer_cast<erhe::scene::Mesh>(att);
        if (mesh) {
            att_json["primitive_count"] = static_cast<int>(mesh->get_primitives().size());
            json mat_names = json::array();
            for (const auto& prim : mesh->get_primitives()) {
                mat_names.push_back(prim.material ? prim.material->get_name() : "(none)");
            }
            att_json["materials"] = mat_names;
        }

        auto camera = std::dynamic_pointer_cast<erhe::scene::Camera>(att);
        if (camera) {
            att_json["exposure"]     = camera->get_exposure();
            att_json["shadow_range"] = camera->get_shadow_range();
        }

        auto light = std::dynamic_pointer_cast<erhe::scene::Light>(att);
        if (light) {
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            att_json["light_type"] = type_str;
            att_json["color"]      = {light->color.x, light->color.y, light->color.z};
            att_json["intensity"]  = light->intensity;
            att_json["range"]      = light->range;
        }

        auto bp = std::dynamic_pointer_cast<Brush_placement>(att);
        if (bp) {
            auto brush = bp->get_brush();
            if (brush) {
                att_json["brush_name"] = brush->get_name();
                att_json["brush_id"]   = brush->get_id();
            }
        }

        attachments.push_back(att_json);
    }

    json children = json::array();
    for (const auto& child : found_node->get_children()) {
        children.push_back(child->get_name());
    }

    auto parent_node = found_node->get_parent_node();

    json result = {
        {"name",           found_node->get_name()},
        {"id",             found_node->get_id()},
        {"parent",         parent_node ? parent_node->get_name() : ""},
        {"world_position", {wp.x, wp.y, wp.z}},
        {"local_transform", {
            {"translation",   {t.x, t.y, t.z}},
            {"rotation_xyzw", {r.x, r.y, r.z, r.w}},
            {"scale",         {s.x, s.y, s.z}}
        }},
        {"attachments",    attachments},
        {"children",       children},
        {"visible",        found_node->is_visible()},
        {"selected",       found_node->is_selected()},
        {"locked",         found_node->is_lock_edit()},
        {"tags",           [&]() { json t = json::array(); for (const auto& tag : found_node->get_tags()) t.push_back(tag); return t; }()}
    };

    return make_json_content(result).dump();
}

auto Mcp_server::query_scene_cameras(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json cameras = json::array();
    for (const auto& camera : sr->get_scene().get_cameras()) {
        const auto* node = camera->get_node();
        cameras.push_back({
            {"name",         camera->get_name()},
            {"id",           camera->get_id()},
            {"node",         node ? node->get_name() : ""},
            {"exposure",     camera->get_exposure()},
            {"shadow_range", camera->get_shadow_range()}
        });
    }

    return make_json_content({{"cameras", cameras}}).dump();
}

auto Mcp_server::query_scene_lights(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    json lights = json::array();
    for (const auto& ll : sr->get_scene().get_light_layers()) {
        for (const auto& light : ll->lights) {
            const auto* node = light->get_node();
            const char* type_str = (light->type == erhe::scene::Light_type::directional) ? "directional"
                                 : (light->type == erhe::scene::Light_type::point)       ? "point"
                                 : (light->type == erhe::scene::Light_type::spot)         ? "spot"
                                 : "unknown";
            lights.push_back({
                {"name",      light->get_name()},
                {"id",        light->get_id()},
                {"node",      node ? node->get_name() : ""},
                {"type",      type_str},
                {"color",     {light->color.x, light->color.y, light->color.z}},
                {"intensity", light->intensity},
                {"range",     light->range}
            });
        }
    }

    return make_json_content({{"lights", lights}}).dump();
}

auto Mcp_server::query_scene_materials(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        return make_json_content({{"materials", json::array()}}).dump();
    }

    json materials = json::array();
    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        materials.push_back({
            {"name",       mat->get_name()},
            {"id",         mat->get_id()},
            {"base_color", {mat->data.base_color.x, mat->data.base_color.y, mat->data.base_color.z}},
            {"metallic",   mat->data.metallic},
            {"roughness",  mat->data.roughness.x},
            {"emissive",   {mat->data.emissive.x, mat->data.emissive.y, mat->data.emissive.z}}
        });
    }

    return make_json_content({{"materials", materials}}).dump();
}

auto Mcp_server::query_material_details(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::string material_name = args.value("material_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->materials) {
        json r = make_text_content("No materials in scene: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
    for (const auto& mat : mat_list) {
        if (mat->get_name() == material_name) {
            const auto& d = mat->data;
            json result = {
                {"name",                       mat->get_name()},
                {"id",                         mat->get_id()},
                {"base_color",                 {d.base_color.x, d.base_color.y, d.base_color.z}},
                {"opacity",                    d.opacity},
                {"roughness",                  {d.roughness.x, d.roughness.y}},
                {"metallic",                   d.metallic},
                {"reflectance",                d.reflectance},
                {"emissive",                   {d.emissive.x, d.emissive.y, d.emissive.z}},
                {"normal_texture_scale",       d.normal_texture_scale},
                {"occlusion_texture_strength", d.occlusion_texture_strength},
                {"unlit",                      d.unlit},
                {"has_base_color_texture",     d.texture_samplers.base_color.texture != nullptr},
                {"has_normal_texture",         d.texture_samplers.normal.texture != nullptr},
                {"has_metallic_roughness_texture", d.texture_samplers.metallic_roughness.texture != nullptr}
            };
            return make_json_content(result).dump();
        }
    }

    json r = make_text_content("Material not found: " + material_name);
    r["isError"] = true;
    return r.dump();
}

auto Mcp_server::query_scene_brushes(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        return make_json_content({{"brushes", json::array()}}).dump();
    }

    json brushes = json::array();
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& brush : brush_list) {
        brushes.push_back({
            {"name", brush->get_name()},
            {"id",   brush->get_id()}
        });
    }

    return make_json_content({{"brushes", brushes}}).dump();
}

auto Mcp_server::query_selection(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.selection) {
        return make_json_content({{"items", json::array()}}).dump();
    }

    json items = json::array();
    for (const auto& item : m_context.selection->get_selected_items()) {
        items.push_back({
            {"name",      item->get_name()},
            {"type",      std::string{item->get_type_name()}},
            {"id",        item->get_id()}
        });
    }

    return make_json_content({{"items", items}}).dump();
}

auto Mcp_server::query_undo_redo_stack(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.operation_stack) {
        return make_json_content({{"undo", json::array()}, {"redo", json::array()}, {"can_undo", false}, {"can_redo", false}}).dump();
    }

    auto make_stack = [](const std::vector<std::shared_ptr<Operation>>& ops) -> json {
        json arr = json::array();
        for (const auto& op : ops) {
            json entry = {{"description", op->describe()}};
            if (op->has_error()) {
                entry["error"] = op->get_error();
            }
            arr.push_back(entry);
        }
        return arr;
    };

    return make_json_content({
        {"undo",     make_stack(m_context.operation_stack->get_undo_stack())},
        {"redo",     make_stack(m_context.operation_stack->get_redo_stack())},
        {"can_undo", m_context.operation_stack->can_undo()},
        {"can_redo", m_context.operation_stack->can_redo()}
    }).dump();
}

auto Mcp_server::query_async_status(const json& args) -> std::string
{
    static_cast<void>(args);

    return make_json_content({
        {"pending", m_context.pending_async_ops.load()},
        {"running", m_context.running_async_ops.load()}
    }).dump();
}

auto Mcp_server::find_items_by_ids(Scene_root& sr, const std::set<std::size_t>& target_ids) -> std::vector<std::shared_ptr<erhe::Item_base>>
{
    std::vector<std::shared_ptr<erhe::Item_base>> result;

    const auto& scene = sr.get_scene();
    for (const auto& node : scene.get_flat_nodes()) {
        if (target_ids.contains(node->get_id())) {
            result.push_back(node);
        }
    }
    for (const auto& camera : scene.get_cameras()) {
        if (target_ids.contains(camera->get_id())) {
            result.push_back(camera);
        }
    }
    for (const auto& ll : scene.get_light_layers()) {
        for (const auto& light : ll->lights) {
            if (target_ids.contains(light->get_id())) {
                result.push_back(light);
            }
        }
    }
    auto library = sr.get_content_library();
    if (library) {
        if (library->materials) {
            for (const auto& mat : library->materials->get_all<erhe::primitive::Material>()) {
                if (target_ids.contains(mat->get_id())) {
                    result.push_back(mat);
                }
            }
        }
        if (library->brushes) {
            for (const auto& brush : library->brushes->get_all<Brush>()) {
                if (target_ids.contains(brush->get_id())) {
                    result.push_back(brush);
                }
            }
        }
    }
    return result;
}

auto Mcp_server::action_select_items(const json& args) -> std::string
{
    if (!m_context.selection) {
        json r = make_text_content("Selection system not available");
        r["isError"] = true;
        return r.dump();
    }

    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& id_val : ids_json) {
        if (id_val.is_number_unsigned() || id_val.is_number_integer()) {
            target_ids.insert(id_val.get<std::size_t>());
        }
    }

    if (target_ids.empty()) {
        m_context.selection->clear_selection();
        return make_text_content("Selection cleared").dump();
    }

    auto items_to_select = find_items_by_ids(*sr, target_ids);
    m_context.selection->set_selection(items_to_select);

    json selected = json::array();
    for (const auto& item : items_to_select) {
        selected.push_back({
            {"name", item->get_name()},
            {"type", std::string{item->get_type_name()}},
            {"id",   item->get_id()}
        });
    }

    return make_json_content({
        {"selected_count", static_cast<int>(items_to_select.size())},
        {"items",          selected}
    }).dump();
}

auto Mcp_server::action_place_brush(const json& args) -> std::string
{
    const std::string scene_name    = args.value("scene_name", "");
    const std::size_t brush_id      = args.value("brush_id", std::size_t{0});
    const json        pos_json      = args.value("position", json::array());
    const std::string material_name = args.value("material_name", "");
    const double      scale         = args.value("scale", 1.0);
    const std::string motion_str    = args.value("motion_mode", "dynamic");

    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }

    // Find brush by ID
    auto library = sr->get_content_library();
    if (!library || !library->brushes) {
        json r = make_text_content("No brushes in scene");
        r["isError"] = true;
        return r.dump();
    }

    std::shared_ptr<Brush> brush;
    const auto& brush_list = library->brushes->get_all<Brush>();
    for (const auto& b : brush_list) {
        if (b->get_id() == brush_id) {
            brush = b;
            break;
        }
    }
    if (!brush) {
        json r = make_text_content("Brush not found with id: " + std::to_string(brush_id));
        r["isError"] = true;
        return r.dump();
    }

    // Parse position
    glm::vec3 position{0.0f};
    if (pos_json.is_array() && pos_json.size() >= 3) {
        position.x = pos_json[0].get<float>();
        position.y = pos_json[1].get<float>();
        position.z = pos_json[2].get<float>();
    }

    // Find material
    std::shared_ptr<erhe::primitive::Material> material;
    if (!material_name.empty() && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        for (const auto& mat : mat_list) {
            if (mat->get_name() == material_name) {
                material = mat;
                break;
            }
        }
    }
    if (!material && library->materials) {
        const auto& mat_list = library->materials->get_all<erhe::primitive::Material>();
        if (!mat_list.empty()) {
            material = mat_list.front();
        }
    }
    if (!material) {
        json r = make_text_content("No materials available");
        r["isError"] = true;
        return r.dump();
    }

    // Motion mode
    erhe::physics::Motion_mode motion_mode = erhe::physics::Motion_mode::e_dynamic;
    if (motion_str == "static") {
        motion_mode = erhe::physics::Motion_mode::e_static;
    }

    const glm::mat4 world_from_node = erhe::math::create_translation<float>(position);

    auto node = place_brush_in_scene(
        m_context,
        *brush,
        *sr,
        world_from_node,
        material,
        scale,
        motion_mode
    );

    if (!node) {
        json r = make_text_content("Failed to place brush");
        r["isError"] = true;
        return r.dump();
    }

    json result = {
        {"node_name", node->get_name()},
        {"node_id",   node->get_id()},
        {"brush",     brush->get_name()},
        {"material",  material->get_name()},
        {"position",  {position.x, position.y, position.z}},
        {"scale",     scale}
    };
    return make_json_content(result).dump();
}

auto Mcp_server::action_toggle_physics(const json& args) -> std::string
{
    static_cast<void>(args);

    if (!m_context.app_settings) {
        json r = make_text_content("Settings not available");
        r["isError"] = true;
        return r.dump();
    }

    m_context.app_settings->physics.dynamic_enable = !m_context.app_settings->physics.dynamic_enable;
    const bool enabled = m_context.app_settings->physics.dynamic_enable;

    return make_json_content({
        {"dynamic_physics_enabled", enabled}
    }).dump();
}

auto Mcp_server::action_lock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(true);
    }
    return make_json_content({{"locked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_unlock_items(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json = args.value("ids", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        item->set_lock_edit(false);
    }
    return make_json_content({{"unlocked_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_add_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->add_tag(tag);
        }
    }
    return make_json_content({{"tagged_count", static_cast<int>(items.size())}}).dump();
}

auto Mcp_server::action_remove_tags(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    auto* sr = find_scene(scene_name);
    if (!sr) {
        json r = make_text_content("Scene not found: " + scene_name);
        r["isError"] = true;
        return r.dump();
    }
    const json ids_json  = args.value("ids", json::array());
    const json tags_json = args.value("tags", json::array());
    std::set<std::size_t> target_ids;
    for (const auto& v : ids_json) {
        if (v.is_number()) target_ids.insert(v.get<std::size_t>());
    }
    std::vector<std::string> tags;
    for (const auto& v : tags_json) {
        if (v.is_string()) tags.push_back(v.get<std::string>());
    }
    auto items = find_items_by_ids(*sr, target_ids);
    for (auto& item : items) {
        for (const auto& tag : tags) {
            item->remove_tag(tag);
        }
    }
    return make_json_content({{"untagged_count", static_cast<int>(items.size())}}).dump();
}

} // namespace editor
