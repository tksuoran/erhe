// Mesh level of detail MCP tools (doc/plans/lod.md step 3,
// doc/erhe/meshoptimizer_integration.md "Level of detail"): generate_lods
// queues on-request LOD generation for the distinct primitives of the given
// meshes (request_lod_generation()), get_lod_info reports a mesh's
// primitives' requests and chains.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "scene/scene_root.hpp"

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>

namespace editor {

using json = nlohmann::json;
using namespace mcp_server_detail;

namespace {

constexpr const char* c_lod_attributes_names[] = { "positions_only", "normals_and_colors" };
constexpr const char* c_lod_border_names    [] = { "free", "locked" };
constexpr const char* c_lod_seams_names     [] = { "keep", "permissive" };
constexpr const char* c_lod_fallback_names  [] = { "none", "sloppy" };

// Index of `name` in `names`; false when it is not there.
template <std::size_t N>
[[nodiscard]] auto find_enum_name(const char* const (&names)[N], const std::string_view name, unsigned int& out_value) -> bool
{
    for (std::size_t i = 0; i < N; ++i) {
        if (name == names[i]) {
            out_value = static_cast<unsigned int>(i);
            return true;
        }
    }
    return false;
}

[[nodiscard]] auto lod_settings_to_json(const erhe::primitive::Lod_settings& settings) -> json
{
    return json{
        {"level_count",        settings.level_count},
        {"ratio",              settings.ratio},
        {"attributes",         c_lod_attributes_names[static_cast<std::size_t>(settings.attributes)]},
        {"normal_weight",      settings.normal_weight},
        {"color_weight",       settings.color_weight},
        {"border",             c_lod_border_names[static_cast<std::size_t>(settings.border)]},
        {"seams",              c_lod_seams_names[static_cast<std::size_t>(settings.seams)]},
        {"fallback",           c_lod_fallback_names[static_cast<std::size_t>(settings.fallback)]},
        {"normal_merge_angle", settings.normal_merge_angle}
    };
}

// Lod_settings from the generate_lods `settings` object: fields not given keep
// the Lod_settings defaults. Empty out_error on success.
[[nodiscard]] auto parse_lod_settings(const json& object, std::string& out_error) -> erhe::primitive::Lod_settings
{
    erhe::primitive::Lod_settings settings{};
    if (!object.is_object()) {
        out_error = "settings must be an object";
        return settings;
    }
    for (json::const_iterator it = object.begin(); it != object.end(); ++it) {
        const std::string& key   = it.key();
        const json&        value = it.value();
        if (key == "level_count") {
            if (!value.is_number_integer() || (value.get<long long>() < 1) || (value.get<long long>() > static_cast<long long>(erhe::primitive::max_lod_levels))) {
                out_error = fmt::format("settings.level_count must be an integer in [1, {}]", erhe::primitive::max_lod_levels);
                return settings;
            }
            settings.level_count = value.get<std::size_t>();
        } else if ((key == "ratio") || (key == "normal_weight") || (key == "color_weight") || (key == "normal_merge_angle")) {
            if (!value.is_number()) {
                out_error = fmt::format("settings.{} must be a number", key);
                return settings;
            }
            const float number = value.get<float>();
            if (key == "ratio") {
                if (!(number > 0.0f) || !(number < 1.0f)) {
                    out_error = "settings.ratio must be in (0, 1)";
                    return settings;
                }
                settings.ratio = number;
            } else if (key == "normal_merge_angle") {
                if (!(number >= 0.0f) || !(number <= 180.0f)) {
                    out_error = "settings.normal_merge_angle must be in [0, 180] degrees";
                    return settings;
                }
                settings.normal_merge_angle = number;
            } else {
                if (!(number >= 0.0f)) {
                    out_error = fmt::format("settings.{} must be >= 0", key);
                    return settings;
                }
                if (key == "normal_weight") {
                    settings.normal_weight = number;
                } else {
                    settings.color_weight = number;
                }
            }
        } else if ((key == "attributes") || (key == "border") || (key == "seams") || (key == "fallback")) {
            if (!value.is_string()) {
                out_error = fmt::format("settings.{} must be a string", key);
                return settings;
            }
            const std::string name = value.get<std::string>();
            unsigned int      enum_value{0};
            bool              found{false};
            if (key == "attributes") {
                found = find_enum_name(c_lod_attributes_names, name, enum_value);
                settings.attributes = static_cast<erhe::primitive::Lod_attributes>(enum_value);
            } else if (key == "border") {
                found = find_enum_name(c_lod_border_names, name, enum_value);
                settings.border = static_cast<erhe::primitive::Lod_border>(enum_value);
            } else if (key == "seams") {
                found = find_enum_name(c_lod_seams_names, name, enum_value);
                settings.seams = static_cast<erhe::primitive::Lod_seams>(enum_value);
            } else {
                found = find_enum_name(c_lod_fallback_names, name, enum_value);
                settings.fallback = static_cast<erhe::primitive::Lod_fallback>(enum_value);
            }
            if (!found) {
                out_error = fmt::format("settings.{}: unknown value '{}'", key, name);
                return settings;
            }
        } else {
            out_error = fmt::format("settings: unknown field '{}'", key);
            return settings;
        }
    }
    return settings;
}

} // anonymous namespace

auto Mcp_server::action_generate_lods(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }

    // Targets: node_ids, or node_id / node_name.
    std::vector<std::shared_ptr<erhe::Item_base>> targets;
    if (args.contains("node_ids")) {
        const json& ids = args["node_ids"];
        if (!ids.is_array() || ids.empty()) {
            return make_error_content("node_ids must be a non-empty array of node ids");
        }
        std::set<std::size_t> target_ids;
        for (const json& id_value : ids) {
            if (!id_value.is_number_unsigned() && !id_value.is_number_integer()) {
                return make_error_content("node_ids entries must be integers");
            }
            target_ids.insert(id_value.get<std::size_t>());
        }
        targets = find_items_by_ids(*scene_root, target_ids);
        if (targets.size() != target_ids.size()) {
            return make_error_content("Some node_ids were not found in scene: " + scene_root->get_name());
        }
    } else {
        const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
        if (!node) {
            return make_error_content("Node not found (give node_ids, node_id or node_name)");
        }
        targets.push_back(node);
    }
    for (const std::shared_ptr<erhe::Item_base>& target : targets) {
        if (!erhe::scene::get_mesh(target)) {
            return make_error_content(fmt::format("Node '{}' has no mesh", target->get_name()));
        }
    }

    const bool clear = args.value("clear", false);
    if (clear && args.contains("settings")) {
        return make_error_content("Give settings or clear, not both");
    }
    std::optional<erhe::primitive::Lod_settings> settings{};
    if (!clear) {
        std::string error;
        settings = args.contains("settings") ? parse_lod_settings(args["settings"], error) : erhe::primitive::Lod_settings{};
        if (!error.empty()) {
            return make_error_content(error);
        }
    }

    const Lod_generation_result result = request_lod_generation(m_context, targets, settings);
    return make_json_content({
        {"mode",                settings.has_value() ? "generate" : "clear"},
        {"settings",            settings.has_value() ? lod_settings_to_json(settings.value()) : json{}},
        {"mesh_count",          result.mesh_count},
        {"primitive_count",     result.primitive_count},
        {"queued_count",        result.queued_count},
        {"task_count",          result.task_count},
        {"unchanged_count",     result.unchanged_count},
        {"held_count",          result.held_count},
        {"not_optimized_count", result.not_optimized_count},
        {"message",             "queued - poll get_async_status until idle, then read get_lod_info"}
    }).dump();
}

auto Mcp_server::query_lod_info(const json& args) -> std::string
{
    const std::string scene_name = args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        return make_error_content("Scene not found: " + scene_name);
    }
    const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
    if (!node) {
        return make_error_content("Node not found (give node_id or node_name)");
    }
    const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node);
    if (!mesh) {
        return make_error_content(fmt::format("Node '{}' has no mesh", node->get_name()));
    }

    json primitives = json::array();
    {
        // lod_settings and optimized_render_shape are main-thread state; the
        // finalize commits write them under the item host lock.
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        for (std::size_t primitive_index = 0, end = mesh_primitives.size(); primitive_index < end; ++primitive_index) {
            const erhe::primitive::Primitive* const primitive = mesh_primitives[primitive_index].primitive.get();
            json entry = {
                {"primitive_index", primitive_index}
            };
            if (primitive == nullptr) {
                entry["has_chain"] = false;
                primitives.push_back(entry);
                continue;
            }
            entry["name"]                  = std::string{primitive->get_name()};
            entry["prepared"]              = primitive->is_prepared();
            entry["has_optimized_variant"] = static_cast<bool>(primitive->optimized_render_shape);
            entry["optimization_hold"]     = primitive->optimization_hold_count;
            entry["lod_settings"]          = primitive->lod_settings.has_value() ? lod_settings_to_json(primitive->lod_settings.value()) : json{};
            json levels = json::array();
            bool has_chain = false;
            if (primitive->optimized_render_shape) {
                const erhe::primitive::Buffer_mesh& optimized_mesh = primitive->optimized_render_shape->get_renderable_mesh();
                const erhe::primitive::Lod_chain&   chain          = optimized_mesh.lod_chain;
                has_chain = !chain.is_empty();
                for (std::size_t level = 0; level < chain.level_count; ++level) {
                    const erhe::primitive::Lod_level& lod_level = chain.levels[level];
                    levels.push_back({
                        {"level",          level},
                        {"first_index",    lod_level.index_range.first_index},
                        {"index_count",    lod_level.index_range.index_count},
                        {"triangle_count", lod_level.index_range.get_triangle_count()},
                        {"error",          lod_level.error}
                    });
                }
                entry["generation_seconds"] = chain.generation_seconds;
            }
            entry["has_chain"] = has_chain;
            entry["levels"]    = levels;
            primitives.push_back(entry);
        }
    }
    return make_json_content({
        {"node",       node->get_name()},
        {"node_id",    node->get_id()},
        {"primitives", primitives}
    }).dump();
}

} // namespace editor
