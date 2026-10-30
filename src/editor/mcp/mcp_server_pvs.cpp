// Mcp_server potentially visible set bake tools (doc/plans/pvs.md "Editor"):
// pvs_bake, pvs_debug_face, get_pvs_bake_status, pvs_cancel, pvs_clear,
// get_pvs_cell.
// get_pvs_status is in mcp_server_scene_query.cpp.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "renderers/pvs_baker.hpp"
#include "scene/scene_root.hpp"

#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/pvs_database.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace editor {

using namespace mcp_server_detail;

namespace {

[[nodiscard]] auto parse_vec3(const json& value) -> std::optional<glm::vec3>
{
    if (!value.is_array() || (value.size() != 3)) {
        return std::nullopt;
    }
    glm::vec3 result{0.0f};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!value[i].is_number()) {
            return std::nullopt;
        }
        const float component = value[i].get<float>();
        if (!std::isfinite(component)) {
            return std::nullopt;
        }
        result[static_cast<glm::vec3::length_type>(i)] = component;
    }
    return result;
}

[[nodiscard]] auto to_json(const glm::vec3& value) -> json
{
    return json::array({value.x, value.y, value.z});
}

[[nodiscard]] auto to_json(const glm::uvec3& value) -> json
{
    return json::array({value.x, value.y, value.z});
}

[[nodiscard]] auto to_json(const Pvs_bake_status& status) -> json
{
    json result{
        {"state",             c_str(status.state)},
        {"scene_name",        status.scene_name},
        {"method",            erhe::scene::c_str(status.method)},
        {"grid_origin",       to_json(status.grid_origin)},
        {"cell_size",         status.cell_size},
        {"cell_counts",       to_json(status.cell_counts)},
        {"cell_count",        status.cell_count},
        {"column_count",      status.column_count},
        {"cells_done",        status.cells_done},
        {"capture_count",     status.capture_count},
        {"captures_done",     status.captures_done},
        {"unmatched_entries", status.unmatched_entries},
        {"uids_assigned",     status.uids_assigned},
        {"cells_with_pvs",    status.cells_with_pvs},
        {"seconds_elapsed",   status.seconds_elapsed},
        {"seconds_remaining", status.seconds_remaining},
        {"debug_face",        status.debug_face}
    };
    if (status.debug_face) {
        result["debug_cell"]           = status.debug_cell;
        result["debug_face_index"]     = status.debug_face_index;
        result["debug_layer"]          = status.debug_layer;
        result["debug_z_near"]         = status.debug_z_near;
        result["debug_z_far"]          = status.debug_z_far;
        result["debug_layer_front"]    = status.debug_layer_front;
        result["debug_layer_back"]     = status.debug_layer_back;
        result["debug_depth_entries"]  = status.debug_depth_entries;
        result["debug_id_entries"]     = status.debug_id_entries;
        result["debug_covered_pixels"] = status.debug_covered_pixels;
        result["debug_distinct_ids"]   = status.debug_distinct_ids;
        result["debug_depth_path"]     = status.debug_depth_path;
        result["debug_id_path"]        = status.debug_id_path;
    }
    if (!status.error.empty()) {
        result["error"] = status.error;
    }
    return result;
}

// The arguments pvs_bake and pvs_debug_face share: the grid and the face
// parameters. Empty on success, else the problem.
[[nodiscard]] auto parse_bake_request(const json& args, Pvs_bake_request& request) -> std::string
{
    if (args.contains("cell_size")) {
        if (!args["cell_size"].is_number()) {
            return "cell_size must be a number";
        }
        request.cell_size = args["cell_size"].get<float>();
    }
    if (args.contains("region_min") != args.contains("region_max")) {
        return "give both region_min and region_max, or neither";
    }
    if (args.contains("region_min")) {
        request.region_min = parse_vec3(args["region_min"]);
        request.region_max = parse_vec3(args["region_max"]);
        if (!request.region_min.has_value() || !request.region_max.has_value()) {
            return "region_min and region_max must be finite [x, y, z]";
        }
    }
    const auto read_uint = [&args](const char* key, uint32_t& out_value) -> bool {
        if (!args.contains(key)) {
            return true;
        }
        if (!args[key].is_number_integer() || (args[key].get<int64_t>() < 0) || (args[key].get<int64_t>() > 0xffffffffll)) {
            return false;
        }
        out_value = static_cast<uint32_t>(args[key].get<int64_t>());
        return true;
    };
    // The disocclusion faces default to 1024 pixels (doc/plans/pvs.md
    // "Bake" 3), the sampled captures to Pvs_bake_request's 512.
    if (request.method == erhe::scene::Pvs_bake_method::disocclusion) {
        request.face_resolution = 1024;
    }
    if (
        !read_uint("samples_per_cell",   request.samples_per_cell)   ||
        !read_uint("face_resolution",    request.face_resolution)    ||
        !read_uint("captures_in_flight", request.captures_in_flight) ||
        !read_uint("layer_count",        request.layer_count)        ||
        !read_uint("tile_size",          request.tile_size)          ||
        !read_uint("samples_per_face",   request.samples_per_face)
    ) {
        return "samples_per_cell, face_resolution, captures_in_flight, layer_count, tile_size and samples_per_face must be non-negative integers";
    }
    if (args.contains("face_fov_degrees")) {
        if (!args["face_fov_degrees"].is_number()) {
            return "face_fov_degrees must be a number";
        }
        request.face_fov = glm::radians(args["face_fov_degrees"].get<float>());
    }
    return {};
}

} // anonymous namespace

auto Mcp_server::action_pvs_bake(const json& args) -> std::string
{
    if ((m_context.pvs_baker == nullptr) || (m_context.app_scenes == nullptr)) {
        return make_error_content("pvs_bake: the PVS baker is not available");
    }
    const std::string scene_name = args.value("scene_name", std::string{});
    std::shared_ptr<Scene_root> scene_root{};
    for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
        if (candidate->get_name() == scene_name) {
            scene_root = candidate;
            break;
        }
    }
    if (!scene_root) {
        return make_error_content("pvs_bake: scene not found: " + scene_name);
    }

    Pvs_bake_request request{};
    const std::string method_name = args.value("method", std::string{"sampled"});
    const std::optional<erhe::scene::Pvs_bake_method> method = erhe::scene::parse_pvs_bake_method(method_name);
    if (!method.has_value()) {
        return make_error_content("pvs_bake: method must be \"sampled\" or \"disocclusion\"");
    }
    request.method = method.value();
    const std::string parse_error = parse_bake_request(args, request);
    if (!parse_error.empty()) {
        return make_error_content("pvs_bake: " + parse_error);
    }

    const std::string error = m_context.pvs_baker->start_bake(scene_root, request);
    if (!error.empty()) {
        return make_error_content("pvs_bake: " + error);
    }
    json result = to_json(m_context.pvs_baker->get_status());
    result["samples_per_cell"]   = request.samples_per_cell;
    result["face_resolution"]    = request.face_resolution;
    result["face_fov_degrees"]   = glm::degrees(request.face_fov);
    result["captures_in_flight"] = request.captures_in_flight;
    if (request.method == erhe::scene::Pvs_bake_method::disocclusion) {
        result["layer_count"]      = request.layer_count;
        result["tile_size"]        = request.tile_size;
        result["samples_per_face"] = request.samples_per_face;
    }
    return make_json_content(result).dump();
}

auto Mcp_server::action_pvs_debug_face(const json& args) -> std::string
{
    if ((m_context.pvs_baker == nullptr) || (m_context.app_scenes == nullptr)) {
        return make_error_content("pvs_debug_face: the PVS baker is not available");
    }
    const std::string scene_name = args.value("scene_name", std::string{});
    std::shared_ptr<Scene_root> scene_root{};
    for (const std::shared_ptr<Scene_root>& candidate : m_context.app_scenes->get_scene_roots()) {
        if (candidate->get_name() == scene_name) {
            scene_root = candidate;
            break;
        }
    }
    if (!scene_root) {
        return make_error_content("pvs_debug_face: scene not found: " + scene_name);
    }
    Pvs_bake_request request{};
    request.method = erhe::scene::Pvs_bake_method::disocclusion;
    const std::string parse_error = parse_bake_request(args, request);
    if (!parse_error.empty()) {
        return make_error_content("pvs_debug_face: " + parse_error);
    }
    Pvs_debug_face_request debug{};
    const auto read_index = [&args](const char* key, uint32_t& out_value) -> bool {
        if (!args.contains(key) || !args[key].is_number_integer() || (args[key].get<int64_t>() < 0) || (args[key].get<int64_t>() > 0xffffffffll)) {
            return false;
        }
        out_value = static_cast<uint32_t>(args[key].get<int64_t>());
        return true;
    };
    if (!read_index("cell", debug.cell) || !read_index("face", debug.face) || !read_index("layer", debug.layer)) {
        return make_error_content("pvs_debug_face: cell, face and layer must be non-negative integers");
    }
    debug.path = args.value("path", std::string{});
    const std::string error = m_context.pvs_baker->start_debug_face(scene_root, request, debug);
    if (!error.empty()) {
        return make_error_content("pvs_debug_face: " + error);
    }
    json result = to_json(m_context.pvs_baker->get_status());
    result["face_resolution"]  = request.face_resolution;
    result["face_fov_degrees"] = glm::degrees(request.face_fov);
    result["layer_count"]      = request.layer_count;
    return make_json_content(result).dump();
}

auto Mcp_server::query_pvs_bake_status(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.pvs_baker == nullptr) {
        return make_error_content("get_pvs_bake_status: the PVS baker is not available");
    }
    return make_json_content(to_json(m_context.pvs_baker->get_status())).dump();
}

auto Mcp_server::action_pvs_cancel(const json& args) -> std::string
{
    static_cast<void>(args);
    if (m_context.pvs_baker == nullptr) {
        return make_error_content("pvs_cancel: the PVS baker is not available");
    }
    const bool was_running = m_context.pvs_baker->is_running();
    m_context.pvs_baker->cancel();
    json result = to_json(m_context.pvs_baker->get_status());
    result["cancelled"] = was_running;
    return make_json_content(result).dump();
}

auto Mcp_server::action_pvs_clear(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", std::string{});
    Scene_root* scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("pvs_clear: scene not found: " + scene_name);
    }
    const bool had_database = (scene_root->get_pvs_database() != nullptr);
    scene_root->set_pvs_database({});
    return make_json_content(json{
        {"scene_name",   scene_root->get_name()},
        {"had_database", had_database},
        {"state",        erhe::scene::c_str(scene_root->get_pvs_state())}
    }).dump();
}

auto Mcp_server::query_pvs_cell(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", std::string{});
    Scene_root* scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("get_pvs_cell: scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Pvs_database> database = scene_root->get_pvs_database();
    if (!database) {
        return make_error_content("get_pvs_cell: the scene has no PVS database");
    }

    uint32_t cell = 0;
    if (args.contains("cell") == args.contains("position")) {
        return make_error_content("get_pvs_cell: give exactly one of cell (index) and position [x, y, z]");
    }
    if (args.contains("cell")) {
        if (!args["cell"].is_number_integer() || (args["cell"].get<int64_t>() < 0) || (args["cell"].get<int64_t>() >= static_cast<int64_t>(database->get_cell_count()))) {
            return make_error_content(fmt::format("get_pvs_cell: cell must be an index in [0, {})", database->get_cell_count()));
        }
        cell = static_cast<uint32_t>(args["cell"].get<int64_t>());
    } else {
        const std::optional<glm::vec3> position = parse_vec3(args["position"]);
        if (!position.has_value()) {
            return make_error_content("get_pvs_cell: position must be a finite [x, y, z]");
        }
        const std::optional<uint32_t> found = database->find_cell(position.value());
        if (!found.has_value()) {
            return make_error_content("get_pvs_cell: the position is outside the grid");
        }
        cell = found.value();
    }

    const glm::uvec3 counts = database->get_header().cell_counts;
    const glm::uvec3 coordinate{cell % counts.x, (cell / counts.x) % counts.y, cell / (counts.x * counts.y)};
    const uint32_t   page   = database->page_of_cell(cell);
    const float      size   = database->get_header().cell_size;
    const glm::vec3  cell_min = database->get_header().grid_origin + (glm::vec3{coordinate} * size);
    json result{
        {"scene_name",  scene_root->get_name()},
        {"cell",        cell},
        {"coordinate",  to_json(coordinate)},
        {"cell_min",    to_json(cell_min)},
        {"cell_max",    to_json(cell_min + glm::vec3{size})},
        {"page",        page},
        {"local_cell",  database->local_cell(cell)},
        {"page_stored", !database->get_page_words(page).empty()},
        {"has_pvs",     database->has_cell_pvs(cell)},
        {"pvs_size",    database->get_cell_pvs_size(cell)}
    };
    json columns = json::array();
    if (database->has_cell_pvs(cell)) {
        // The live mesh names by uid.
        std::unordered_map<std::string, std::string> names;
        {
            const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{scene_root->item_host_mutex};
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root->layers().content()->meshes) {
                if (!mesh->get_gltf_uid().empty()) {
                    names.emplace(mesh->get_gltf_uid(), mesh->get_name());
                }
            }
        }
        const std::vector<erhe::scene::Pvs_column>& database_columns = database->get_columns();
        for (uint32_t column = 0, end = database->get_column_count(); column < end; ++column) {
            if (!database->is_column_visible(cell, column)) {
                continue;
            }
            const erhe::scene::Pvs_column& entry = database_columns[column];
            const std::unordered_map<std::string, std::string>::const_iterator it = names.find(entry.node_uid);
            columns.push_back(json{
                {"node_uid",        entry.node_uid},
                {"node_name",       (it != names.end()) ? it->second : std::string{}},
                {"primitive_index", entry.primitive_index}
            });
        }
    }
    result["columns"] = std::move(columns);
    return make_json_content(result).dump();
}

} // namespace editor
