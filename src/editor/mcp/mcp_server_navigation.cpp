// Navigation mesh MCP tools (doc/editor/navigation.md "MCP").

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_item_bits.hpp"
#include "editor_log.hpp"
#include "navigation/navigation_seeds.hpp"
#include "operations/operation.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_save_service.hpp"
#if defined(ERHE_USE_DETOUR)
#   include "assets/navigation_item.hpp"
#   include "config/generated/debug_visualizations_settings.hpp"
#   include "config/generated/navigation_mesh_visualization.hpp"
#   include "navigation/navigation_mesh.hpp"
#   include "scene/scene_navigation.hpp"
#   include "scene/viewport_scene_view.hpp"
#   include "scene/viewport_scene_views.hpp"
#   include "tools/debug_visualizations.hpp"
#   include "windows/viewport_window.hpp"
#endif
#if defined(ERHE_USE_RECAST)
#   include "scene/navigation_build_job.hpp"
#endif

#include "erhe_item/item.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

// The one open scene named `scene_name`; out_error names an unknown or ambiguous name.
[[nodiscard]] auto find_unique_scene(App_context& context, const std::string& scene_name, std::string& out_error) -> std::shared_ptr<Scene_root>
{
    out_error.clear();
    if (scene_name.empty()) {
        out_error = "'scene_name' is required";
        return {};
    }
    std::shared_ptr<Scene_root> found{};
    std::size_t                 match_count{0};
    if (context.app_scenes != nullptr) {
        for (const std::shared_ptr<Scene_root>& scene_root : context.app_scenes->get_scene_roots()) {
            if (scene_root->get_name() == scene_name) {
                if (!found) {
                    found = scene_root;
                }
                ++match_count;
            }
        }
    }
    if (!found) {
        out_error = "Scene not found: " + scene_name;
        return {};
    }
    if (match_count > 1) {
        out_error = "Scene name '" + scene_name + "' matches " + std::to_string(match_count) + " open scenes";
        return {};
    }
    return found;
}

[[nodiscard]] auto make_seed_json(const Scene_root& scene_root, const erhe::scene::Node& node) -> json
{
    const glm::vec3 point = get_navigation_seed_point(scene_root, node);
    return json{
        {"node",  node.get_name()},
        {"id",    node.get_id()},
        {"uid",   node.get_gltf_uid()},
        {"point", json::array({point.x, point.y, point.z})}
    };
}

#if defined(ERHE_USE_DETOUR)
[[nodiscard]] auto make_vec3_json(const glm::vec3& v) -> json
{
    return json::array({v.x, v.y, v.z});
}

[[nodiscard]] auto make_seed_result_json(const Navigation_seed_result& result) -> json
{
    if (!result.found) {
        return json{{"found", false}, {"reason", result.reason}};
    }
    return json{{"found", true}, {"polygon", result.polygon}, {"snapped_point", make_vec3_json(result.point)}};
}

[[nodiscard]] auto make_navigation_status_json(const Scene_navigation& navigation) -> json
{
    const Navigation_status status = navigation.get_status();
    json stale_reasons = json::array();
    if (status.staleness.geometry_changed) { stale_reasons.push_back("geometry changed"); }
    if (status.staleness.settings_changed) { stale_reasons.push_back("settings changed"); }
    if (status.staleness.seeds_changed)    { stale_reasons.push_back("seeds changed"); }
    json skipped_meshes = json::array();
    for (const std::string& name : status.statistics.skipped_meshes) {
        skipped_meshes.push_back(name);
    }
    json built_seeds = json::array();
    std::vector<Navigation_seed>        seeds;
    std::vector<Navigation_seed_result> results;
    navigation.get_seeds(seeds);
    navigation.get_seed_results(results);
    for (std::size_t i = 0, end = seeds.size(); i < end; ++i) {
        json seed = json{
            {"node",  seeds[i].name},
            {"uid",   seeds[i].uid},
            {"point", make_vec3_json(seeds[i].point)}
        };
        if (i < results.size()) {
            seed["result"] = make_seed_result_json(results[i]);
        }
        built_seeds.push_back(seed);
    }
    return json{
        {"available",               status.available},
        {"stale",                   status.staleness.is_stale()},
        {"stale_reasons",           stale_reasons},
        {"polygon_count",           status.totals.polygon_count},
        {"reachable_polygon_count", status.totals.reachable_polygon_count},
        {"total_area",              status.totals.total_area},
        {"reachable_area",          status.totals.reachable_area},
        {"tile_count",              status.statistics.tile_count},
        {"input_mesh_count",        status.statistics.input_mesh_count},
        {"input_shape_count",       status.statistics.input_shape_count},
        {"input_triangle_count",    status.statistics.input_triangle_count},
        {"skipped_mesh_count",      status.statistics.skipped_mesh_count},
        {"skipped_meshes",          skipped_meshes},
        {"build_ms",                status.statistics.build_ms},
        {"flood_ms",                status.statistics.flood_ms},
        {"build_count",             status.build_count},
        {"flood_count",             status.flood_count},
        {"built_seeds",             built_seeds},
        {"item",                    status.item.has_value()
            ? json{{"key", status.item->key}, {"made", status.item->made}, {"bytes", status.item->bytes}}
            : json(nullptr)},
        {"status_text",             navigation.get_status_text()}
    };
}
#endif

} // anonymous namespace

auto Mcp_server::action_navigation_set_seed(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<Scene_root> scene_root = find_unique_scene(m_context, args.value("scene_name", ""), error);
    if (!scene_root) {
        return make_error_content(error);
    }
    const std::shared_ptr<erhe::Hierarchy> prim = find_unique_prim_in_scene(*scene_root, args, "node_id", "node", "Node", Absent_prim::error, error);
    if (!prim) {
        return make_error_content(error);
    }
    const std::shared_ptr<erhe::scene::Node> node = std::dynamic_pointer_cast<erhe::scene::Node>(prim);
    if (!node) {
        return make_error_content("'" + prim->get_name() + "' is not a node (a prim without a transform cannot be a navigation seed)");
    }
    if (node == scene_root->get_scene().get_root_node()) {
        return make_error_content("The scene root node cannot be a navigation seed");
    }
    if (args.contains("seed") && !args.at("seed").is_boolean()) {
        return make_error_content("'seed' must be true or false");
    }
    const Navigation_seed_state state = args.value("seed", true) ? Navigation_seed_state::marked : Navigation_seed_state::unmarked;
    const std::shared_ptr<Operation> operation = make_navigation_seed_operation(
        std::vector<std::shared_ptr<erhe::Item_base>>{node},
        state
    );
    if (operation) {
        m_context.operation_stack->execute_now(operation);
    }
    log_mcp->info(
        "navigation_set_seed: '{}' in '{}' {} ({})",
        node->get_name(), scene_root->get_name(),
        (state == Navigation_seed_state::marked) ? "marked" : "unmarked",
        operation ? "changed" : "unchanged"
    );
    json result = make_seed_json(*scene_root, *node);
    result["scene_name"] = scene_root->get_name();
    result["seed"]       = ((node->get_flag_bits() & Editor_item_flags::navigation_seed) != 0u);
    result["changed"]    = static_cast<bool>(operation);
    return make_json_content(result).dump();
}

auto Mcp_server::query_navigation_status(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<Scene_root> scene_root = find_unique_scene(m_context, args.value("scene_name", ""), error);
    if (!scene_root) {
        return make_error_content(error);
    }
    std::vector<std::shared_ptr<erhe::scene::Node>> seed_nodes;
    scene_root->get_navigation_seed_nodes(seed_nodes);
    json seeds = json::array();
    for (const std::shared_ptr<erhe::scene::Node>& node : seed_nodes) {
        seeds.push_back(make_seed_json(*scene_root, *node));
    }
    json reply{
        {"scene_name", scene_root->get_name()},
        {"available",  false},
        {"pending",    (m_context.scene_save_service != nullptr) && m_context.scene_save_service->is_navigation_pending(*scene_root)},
        {"seed_count", seed_nodes.size()},
        {"seeds",      seeds}
    };
#if defined(ERHE_USE_DETOUR)
    const json navigation_json = make_navigation_status_json(scene_root->get_navigation());
    for (const auto& [key, value] : navigation_json.items()) {
        reply[key] = value;
    }
#endif
    return make_json_content(reply).dump();
}

#if defined(ERHE_USE_RECAST)
// Build Navigation Mesh (doc/editor/navigation.md "Job"): a job reported through get_save_status.
auto Mcp_server::action_navigation_build(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<Scene_root> scene_root = find_unique_scene(m_context, args.value("scene_name", ""), error);
    if (!scene_root) {
        return make_error_content(error);
    }
    const std::string refusal = get_navigation_build_refusal(*scene_root);
    if (!refusal.empty()) {
        return make_error_content("navigation_build: " + refusal);
    }
    json job;
    {
        std::lock_guard<std::mutex> lock{m_save_jobs_mutex};
        const std::uint64_t job_id = m_next_save_job_id++;
        job = json{
            {"job_id",     job_id},
            {"tool",       "navigation_build"},
            {"scene_name", scene_root->get_name()},
            {"path",       ""},
            {"format",     "navigation"},
            {"state",      "pending"},
            {"saved",      false}
        };
        m_save_jobs.push_back(job);
        while (m_save_jobs.size() > k_max_save_jobs) {
            m_save_jobs.pop_front();
        }
    }
    const std::uint64_t job_id = job["job_id"].get<std::uint64_t>();
    request_navigation_build(
        m_context, scene_root,
        [this, job_id]() {
            update_save_job(job_id, json{{"state", "running"}});
            const std::shared_ptr<const Scene_service_job> running_job = m_context.scene_save_service->get_running_service_job();
            if (running_job) {
                std::lock_guard<std::mutex> lock{m_save_jobs_mutex};
                m_running_service_jobs[job_id] = running_job;
            }
        },
        [this, job_id, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}](const Scene_save_result& result) {
            std::shared_ptr<const Scene_service_job> finished_job; // released outside the lock
            {
                std::lock_guard<std::mutex> lock{m_save_jobs_mutex};
                const auto running_job = m_running_service_jobs.find(job_id);
                if (running_job != m_running_service_jobs.end()) {
                    finished_job = std::move(running_job->second);
                    m_running_service_jobs.erase(running_job);
                }
            }
            if (!result.saved) {
                update_save_job(job_id, json{{"state", "failed"}, {"save_ms", result.save_ms}, {"error", result.error}});
                log_mcp->warn("MCP server: navigation_build job {} failed: {}", job_id, result.error);
                return;
            }
            json fields{{"state", "done"}, {"saved", true}, {"save_ms", result.save_ms}};
            const std::shared_ptr<Scene_root> finished_scene_root = weak_scene_root.lock();
            if (finished_scene_root) {
                fields["navigation"] = make_navigation_status_json(finished_scene_root->get_navigation());
            }
            update_save_job(job_id, fields);
            log_mcp->info("MCP server: navigation_build job {} done in {:.1f} ms", job_id, result.save_ms);
        }
    );
    log_mcp->info("MCP server: navigation_build job {} queued for '{}'", job_id, scene_root->get_name());
    job["message"] = "queued - poll get_save_status {job_id} until state is done or failed";
    return make_json_content(job).dump();
}
#endif

#if defined(ERHE_USE_DETOUR)
auto Mcp_server::action_navigation_clear(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<Scene_root> scene_root = find_unique_scene(m_context, args.value("scene_name", ""), error);
    if (!scene_root) {
        return make_error_content(error);
    }
    Scene_navigation& navigation = scene_root->get_navigation();
    const bool was_available = static_cast<bool>(navigation.get_mesh());
    clear_navigation(m_context, *scene_root);
    log_mcp->info("navigation_clear: '{}' ({})", scene_root->get_name(), was_available ? "cleared" : "had none");
    return make_json_content(json{{"scene_name", scene_root->get_name()}, {"cleared", was_available}}).dump();
}

// The per-view Navigation Mesh setting of the Debug Visualization popup (doc/editor/navigation.md "Visualization").
auto Mcp_server::action_navigation_set_visualization(const json& args) -> std::string
{
    if (m_context.scene_views == nullptr) {
        return make_error_content("No scene views available");
    }
    Navigation_mesh_visualization mode{Navigation_mesh_visualization::reachable};
    const std::string mode_name = (args.contains("mode") && args.at("mode").is_string()) ? args.at("mode").get<std::string>() : std::string{};
    if (!from_string(mode_name, mode)) {
        return make_error_content("'mode' must be \"off\", \"reachable\" or \"all\"");
    }
    const std::string viewport_title = args.value("viewport", std::string{});
    json viewports = json::array();
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> scene_view = viewport_window->viewport_scene_view();
        if (!scene_view || (!viewport_title.empty() && (viewport_window->get_title() != viewport_title))) {
            continue;
        }
        scene_view->get_debug_visualizations().edit_settings().navigation_mesh = mode;
        viewports.push_back(json{{"viewport", viewport_window->get_title()}, {"mode", std::string{to_string(mode)}}});
    }
    if (viewports.empty()) {
        return make_error_content(
            viewport_title.empty()
                ? std::string{"No viewport scene views available"}
                : ("No viewport window titled '" + viewport_title + "' (see get_viewports)")
        );
    }
    log_mcp->info("navigation_set_visualization: {} in {} viewport(s)", to_string(mode), viewports.size());
    return make_json_content(json{{"viewports", viewports}}).dump();
}

auto Mcp_server::query_navigation_mesh(const json& args) -> std::string
{
    std::string error;
    const std::shared_ptr<Scene_root> scene_root = find_unique_scene(m_context, args.value("scene_name", ""), error);
    if (!scene_root) {
        return make_error_content(error);
    }
    if (args.contains("reachable_only") && !args.at("reachable_only").is_boolean()) {
        return make_error_content("'reachable_only' must be true or false");
    }
    if (args.contains("max_polygons") && !args.at("max_polygons").is_number_unsigned()) {
        return make_error_content("'max_polygons' must be a non-negative integer");
    }
    const bool        reachable_only = args.value("reachable_only", false);
    const std::size_t max_polygons   = args.value("max_polygons", std::size_t{1000});
    const std::shared_ptr<Navigation_mesh> mesh = scene_root->get_navigation().get_mesh();
    if (!mesh) {
        return make_json_content(json{{"scene_name", scene_root->get_name()}, {"available", false}}).dump();
    }
    const std::span<const Navigation_polygon> polygons       = mesh->get_polygons();
    const std::span<const glm::vec3>          vertices       = mesh->get_vertices();
    const std::span<const uint32_t>           vertex_indices = mesh->get_polygon_vertex_indices();
    json        polygons_json = json::array();
    std::size_t matching      = 0;
    for (std::size_t i = 0, end = polygons.size(); i < end; ++i) {
        const Navigation_polygon& polygon = polygons[i];
        if (reachable_only && !polygon.reachable) {
            continue;
        }
        ++matching;
        if (polygons_json.size() >= max_polygons) {
            continue;
        }
        json polygon_vertices = json::array();
        for (uint32_t v = 0; v < polygon.vertex_count; ++v) {
            polygon_vertices.push_back(make_vec3_json(vertices[vertex_indices[polygon.first_vertex + v]]));
        }
        polygons_json.push_back(json{
            {"index",     i},
            {"tile",      polygon.tile},
            {"vertices",  polygon_vertices},
            {"area",      polygon.area},
            {"reachable", polygon.reachable}
        });
    }
    return make_json_content(json{
        {"scene_name",     scene_root->get_name()},
        {"available",      true},
        {"bounds_min",     make_vec3_json(mesh->get_bounds_min())},
        {"bounds_max",     make_vec3_json(mesh->get_bounds_max())},
        {"polygon_count",  polygons.size()},
        {"matching_count", matching},
        {"returned_count", polygons_json.size()},
        {"truncated",      polygons_json.size() < matching},
        {"polygons",       polygons_json}
    }).dump();
}
#endif

} // namespace editor
