// Mesh level of detail MCP tools (doc/plans/lod.md step 3,
// doc/erhe/meshoptimizer_integration.md "Level of detail"): generate_lods
// queues on-request LOD generation for the distinct primitives of the given
// meshes (request_lod_generation()), get_lod_info reports a mesh's
// primitives' requests and chains.

#include "mcp/mcp_server.hpp"
#include "mcp/mcp_server_shared.hpp"
#include "app_context.hpp"
#include "app_rendering.hpp"
#include "renderers/composition_pass.hpp"
#include "renderers/lod_drawn_levels.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "scene/scene_root.hpp"
#include "windows/lod_window.hpp"

#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/lod.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/lod.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_graphics/device.hpp"

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

constexpr const char* c_lod_mode_names      [] = { "automatic", "forced", "off" };
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

[[nodiscard]] auto lod_data_to_json(const erhe::scene::Lod_data& data) -> json
{
    return json{
        {"mode",         c_lod_mode_names[static_cast<std::size_t>(data.mode)]},
        {"forced_level", data.forced_level},
        {"bias",         data.bias},
        {"min_level",    data.min_level},
        {"max_level",    data.max_level},
        {"level",        erhe::scene::get_forced_lod_level(data)}
    };
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
    // The first pass builds the reply and requests the mesh's drawn levels
    // (Lod_drawn_levels, doc/plans/lod_gpu_selection.md section 5); the
    // re-runs request them again until a rendered frame recorded them, then
    // add them to the reply.
    const bool continuation =
        (m_lod_info_request != nullptr) &&
        (m_lod_info_request == m_current_request) &&
        (m_lod_info_enqueued_at == m_current_request->enqueued_at);
    Lod_drawn_levels& lod_drawn_levels = m_context.app_rendering->get_lod_drawn_levels();
    if (continuation) {
        const std::shared_ptr<erhe::scene::Mesh> mesh       = m_lod_info_mesh.lock();
        const std::shared_ptr<Scene_root>        scene_root = m_lod_info_scene_root.lock();
        const uint64_t                           frame      = m_context.graphics_device->get_frame_index();
        const std::optional<uint64_t>            served     = lod_drawn_levels.get_served_frame_index(Lod_drawn_levels_requester::mcp);
        const std::optional<uint64_t>            rendered   = m_context.last_rendered_frame_index;
        // The request is renewed every pass, so every frame from the first
        // one that served it has the mesh's queries.
        if (!m_lod_info_first_served.has_value() && served.has_value() && (served.value() >= m_lod_info_request_frame)) {
            m_lod_info_first_served = served;
        }
        const Lod_drawn_levels_frame* const drawn_frame =
            (m_lod_info_first_served.has_value() && rendered.has_value() && (rendered.value() >= m_lod_info_first_served.value()))
                ? lod_drawn_levels.find(rendered)
                : nullptr;
        // A scene no view renders records no levels: give up after a few
        // frames, with no drawn levels.
        constexpr uint64_t max_wait_frames = 16;
        const bool timed_out = (frame > (m_lod_info_request_frame + max_wait_frames));
        if ((drawn_frame == nullptr) && !timed_out && mesh && scene_root) {
            const std::shared_ptr<erhe::scene::Mesh> meshes[1]{mesh};
            lod_drawn_levels.request(Lod_drawn_levels_requester::mcp, scene_root, meshes);
            m_defer_current_request = true;
            return {};
        }
        m_lod_info_request = nullptr;
        json reply = std::move(m_lod_info_reply);
        m_lod_info_reply = json{};
        // The levels per view of the frame, a view named by its scene
        // view's name (the viewports; "?" for another view).
        json drawn = json::array();
        if (drawn_frame != nullptr) {
            for (const Lod_drawn_level& level : drawn_frame->levels) {
                if (level.mesh_id != m_lod_info_mesh_id) {
                    continue;
                }
                std::string view_name{"?"};
                if (m_context.scene_views != nullptr) {
                    for (const std::shared_ptr<Viewport_scene_view>& view : m_context.scene_views->get_viewport_scene_views()) {
                        if (view && (get_lod_view_key(*view) == level.view_key)) {
                            view_name = view->get_name();
                            break;
                        }
                    }
                }
                drawn.push_back(json{
                    {"view",                 view_name},
                    {"mesh_primitive_index", level.mesh_primitive_index},
                    {"level",                level.drawn.level},
                    {"source",               erhe::scene_renderer::c_str(level.drawn.source)}
                });
            }
        }
        reply["drawn_frame_index"] = (drawn_frame != nullptr) ? json(drawn_frame->frame_index) : json(nullptr);
        reply["drawn"]             = std::move(drawn);
        return make_json_content(reply).dump();
    }

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
    json lod        = json{};
    json draw_lists = json{};
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
                        // Levels > 0; level 0 draws the original variant's edge lines.
                        {"edge_count",     lod_level.edge_line_vertex_range.count / 2},
                        {"error",          lod_level.error}
                    });
                }
                entry["generation_seconds"] = chain.generation_seconds;
            }
            entry["has_chain"] = has_chain;
            entry["levels"]    = levels;
            primitives.push_back(entry);
        }

        // The effective Lod record (erhe::scene::resolve_lod: the values of
        // the mesh when it or an ancestor carries Lod.apply_lod) and what the
        // draw lists hold: the record they were handed and, per entry, the
        // chain level whose range the entry copied at registration.
        lod = lod_data_to_json(erhe::scene::resolve_lod(*mesh));
        const erhe::scene_renderer::Draw_list_scene* const draw_list_scene = scene_root->get_draw_list_scene();
        if (draw_list_scene != nullptr) {
            json entries = json::array();
            const erhe::scene_renderer::Draw_list_object* const object = draw_list_scene->get_object(draw_list_scene->find_object(mesh.get()));
            if (object != nullptr) {
                for (const erhe::scene_renderer::Draw_list_entry_location& location : object->locations) {
                    const erhe::scene_renderer::Draw_list&       draw_list = draw_list_scene->get_draw_lists()[location.draw_list_index];
                    const erhe::scene_renderer::Draw_list_entry& list_entry = draw_list.entries[location.entry_index];
                    int level = -1;
                    if (list_entry.mesh_primitive_index < mesh_primitives.size()) {
                        const erhe::primitive::Primitive* const primitive = mesh_primitives[list_entry.mesh_primitive_index].primitive.get();
                        if (primitive != nullptr) {
                            const erhe::primitive::Buffer_mesh* const buffer_mesh = primitive->get_resolved_renderable_mesh(
                                list_entry.variant,
                                erhe::primitive::Primitive_mode::polygon_fill
                            ).second;
                            if (buffer_mesh != nullptr) {
                                level = find_draw_list_entry_lod_level(list_entry, *buffer_mesh);
                            }
                        }
                    }
                    entries.push_back({
                        {"mesh_primitive_index", list_entry.mesh_primitive_index},
                        {"purpose",              erhe::scene_renderer::c_str(draw_list.key.purpose)},
                        {"variant",              erhe::primitive::c_str(list_entry.variant)},
                        {"first_index",          list_entry.first_index},
                        {"index_count",          list_entry.index_count},
                        {"triangle_count",       list_entry.index_count / 3u},
                        {"level",                level}
                    });
                }
            }
            draw_lists = json{
                {"registered", object != nullptr},
                {"lod",        lod_data_to_json(draw_list_scene->get_mesh_lod(mesh.get()))},
                {"entries",    entries}
            };
        }
    }
    m_lod_info_reply = json{
        {"node",       node->get_name()},
        {"node_id",    node->get_id()},
        {"lod",        lod},
        {"draw_lists", draw_lists},
        {"primitives", primitives}
    };
    m_lod_info_mesh          = mesh;
    m_lod_info_mesh_id       = mesh->get_id();
    m_lod_info_scene_root    = scene_root->shared_from_this();
    m_lod_info_request_frame = m_context.graphics_device->get_frame_index();
    m_lod_info_first_served  = std::nullopt;
    {
        const std::shared_ptr<erhe::scene::Mesh> meshes[1]{mesh};
        lod_drawn_levels.request(Lod_drawn_levels_requester::mcp, scene_root->shared_from_this(), meshes);
    }
    m_lod_info_request      = m_current_request;
    m_lod_info_enqueued_at  = m_current_request->enqueued_at;
    m_defer_current_request = true;
    return {};
}

auto Mcp_server::query_lod_stats(const json& args) -> std::string
{
    // Per draw-list color pass of the last rendered frame, the entries it
    // drew per level of detail and their triangles
    // (Composition_pass_stats::lod_counts; doc/plans/lod_gpu_selection.md
    // section 5): counted by the CPU command writer, or from the GPU
    // compaction's per-job counts of a retired frame (counts_frame_index).
    const std::string scene_view_filter = args.value("scene_view", "");
    json passes = json::array();
    erhe::scene_renderer::Lod_level_counts total{};
    for (const std::shared_ptr<Composition_pass>& pass : m_context.app_rendering->composition_passes()) {
        if (!pass) {
            continue;
        }
        const Composition_pass_stats* const stats = pass->get_frame_stats(m_context.last_rendered_frame_index);
        if ((stats == nullptr) || (stats->result != Composition_pass_result::submitted_draw_lists)) {
            continue;
        }
        if (!scene_view_filter.empty() && (stats->scene_view_name != scene_view_filter)) {
            continue;
        }
        json levels = json::array();
        uint64_t entry_count    = 0;
        uint64_t triangle_count = 0;
        for (std::size_t level = 0; level < erhe::scene_renderer::Lod_level_counts::level_count; ++level) {
            levels.push_back(json{
                {"level",     level},
                {"entries",   stats->lod_counts.entries[level]},
                {"triangles", stats->lod_counts.triangles[level]}
            });
            entry_count    += stats->lod_counts.entries[level];
            triangle_count += stats->lod_counts.triangles[level];
        }
        total.add(stats->lod_counts);
        passes.push_back(json{
            {"name",               pass->get_name()},
            {"scene_view",         stats->scene_view_name},
            {"gpu_generated",      stats->draw_list_gpu_generated},
            {"counts_ready",       stats->draw_list_counts_ready},
            {"counts_frame_index", stats->draw_list_counts_frame_index},
            {"entry_count",        entry_count},
            {"triangle_count",     triangle_count},
            {"drawn_entry_count",  stats->draw_list_entry_count},
            {"levels",             std::move(levels)}
        });
    }
    json total_levels = json::array();
    uint64_t total_entries   = 0;
    uint64_t total_triangles = 0;
    for (std::size_t level = 0; level < erhe::scene_renderer::Lod_level_counts::level_count; ++level) {
        total_levels.push_back(json{
            {"level",     level},
            {"entries",   total.entries[level]},
            {"triangles", total.triangles[level]}
        });
        total_entries   += total.entries[level];
        total_triangles += total.triangles[level];
    }
    return make_json_content(json{
        {"frame_index",    m_context.last_rendered_frame_index.has_value() ? json(m_context.last_rendered_frame_index.value()) : json(nullptr)},
        {"passes",         std::move(passes)},
        {"entry_count",    total_entries},
        {"triangle_count", total_triangles},
        {"levels",         std::move(total_levels)}
    }).dump();
}

auto Mcp_server::query_lod_selection(const json& args) -> std::string
{
    // The CPU level region of one classified frame for one mesh
    // (doc/plans/lod_gpu_selection.md step 2a): the first pass requests the
    // capture and defers; the re-runs poll until a frame copied it.
    const bool continuation =
        (m_lod_selection_request != nullptr) &&
        (m_lod_selection_request == m_current_request) &&
        (m_lod_selection_enqueued_at == m_current_request->enqueued_at);
    const std::string scene_name = continuation ? m_lod_selection_scene_name : args.value("scene_name", "");
    Scene_root* const scene_root = find_scene(scene_name);
    if (scene_root == nullptr) {
        m_lod_selection_request = nullptr;
        return make_error_content("Scene not found: " + scene_name);
    }
    erhe::scene_renderer::Draw_list_scene* const draw_list_scene = scene_root->get_draw_list_scene();
    if (draw_list_scene == nullptr) {
        m_lod_selection_request = nullptr;
        return make_error_content("get_lod_selection: the scene has no draw lists");
    }

    if (!continuation) {
        const std::shared_ptr<erhe::scene::Node> node = find_node_in_scene(*scene_root, args, "node_id", "node_name");
        if (!node) {
            return make_error_content("Node not found (give node_id or node_name)");
        }
        const std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(node);
        if (!mesh) {
            return make_error_content(fmt::format("Node '{}' has no mesh", node->get_name()));
        }
        erhe::scene_renderer::Draw_list_object_id object{};
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
            object = draw_list_scene->find_object(mesh.get());
        }
        if (!object.is_valid()) {
            return make_error_content(fmt::format("get_lod_selection: mesh '{}' is not registered in the draw lists", mesh->get_name()));
        }
        draw_list_scene->request_lod_level_capture(object);
        m_lod_selection_request     = m_current_request;
        m_lod_selection_enqueued_at = m_current_request->enqueued_at;
        m_lod_selection_scene_name  = scene_name;
        m_defer_current_request     = true;
        return {};
    }

    erhe::scene_renderer::Lod_level_capture capture{};
    const erhe::scene_renderer::Cull_classification_check_state state = draw_list_scene->poll_lod_level_capture(capture);
    if (state == erhe::scene_renderer::Cull_classification_check_state::pending) {
        m_defer_current_request = true;
        return {};
    }
    m_lod_selection_request = nullptr;
    if (state != erhe::scene_renderer::Cull_classification_check_state::complete) {
        return make_error_content("get_lod_selection: the capture was dropped");
    }

    json columns = json::array();
    for (const erhe::scene_renderer::Lod_level_capture_column& column : capture.columns) {
        columns.push_back(json{
            {"column",           column.column},
            {"view",             column.view},
            {"origin",           json::array({column.lod_view.origin.x, column.lod_view.origin.y, column.lod_view.origin.z})},
            {"projection_scale", column.lod_view.projection_scale},
            {"projection",       erhe::scene_renderer::c_str(column.lod_view.projection)}
        });
    }
    json entries = json::array();
    for (const erhe::scene_renderer::Lod_level_capture_entry& entry : capture.entries) {
        const erhe::scene_renderer::Lod_word_fields lod_fields = erhe::scene_renderer::decode_lod_word(entry.lod_word);
        entries.push_back(json{
            {"list_index",           entry.list_index},
            {"entry_index",          entry.entry_index},
            {"purpose",              erhe::scene_renderer::c_str(entry.purpose)},
            {"mesh_primitive_index", entry.mesh_primitive_index},
            {"center",               json::array({entry.center.x, entry.center.y, entry.center.z})},
            {"extent",               json::array({entry.extent.x, entry.extent.y, entry.extent.z})},
            {"max_axis_scale",       entry.max_axis_scale},
            {"lod_table_index",      (entry.lod_table_index == erhe::scene_renderer::lod_table_index_none) ? json(nullptr) : json(entry.lod_table_index)},
            {"mode",                 lod_fields.mode},
            {"level_count",          lod_fields.level_count},
            {"levels",               entry.levels}
        });
    }
    return make_json_content(json{
        {"scene",                 scene_root->get_name()},
        {"frame_index",           capture.frame_index},
        {"active",                capture.active},
        {"selection",             erhe::scene_renderer::c_str(capture.settings.selection)},
        {"threshold_pixels",      capture.settings.threshold_pixels},
        {"bias",                  capture.settings.bias},
        {"column_overflow_count", capture.column_overflow_count},
        {"columns",               std::move(columns)},
        {"entries",               std::move(entries)}
    }).dump();
}

} // namespace editor
