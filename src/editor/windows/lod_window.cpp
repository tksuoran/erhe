#include "windows/lod_window.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "operations/async_raytrace_kickoff_operation.hpp"
#include "operations/compound_operation.hpp"
#include "operations/operation_stack.hpp"
#include "operations/property_set_operation.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_item/item.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_math/viewport.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/lod.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene_renderer/draw_list_scene.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace editor {

auto find_draw_list_entry_lod_level(
    const erhe::scene_renderer::Draw_list_entry& entry,
    const erhe::primitive::Buffer_mesh&          buffer_mesh
) -> int
{
    const erhe::primitive::Lod_chain& chain = buffer_mesh.lod_chain;
    const std::size_t level_count = chain.is_empty() ? std::size_t{1} : chain.level_count;
    for (std::size_t level = 0; level < level_count; ++level) {
        const erhe::primitive::Index_range range = chain.is_empty()
            ? buffer_mesh.triangle_fill_indices
            : chain.levels[level].index_range;
        if (
            (entry.index_count == static_cast<uint32_t>(range.index_count)) &&
            (entry.first_index == (static_cast<uint32_t>(range.first_index) + buffer_mesh.base_index()))
        ) {
            return static_cast<int>(level);
        }
    }
    return -1;
}

namespace {

constexpr const char* c_attributes_items[] = { "Positions only", "Normals and colors" };
constexpr const char* c_border_items    [] = { "Free", "Locked" };
constexpr const char* c_seams_items     [] = { "Keep", "Permissive" };
constexpr const char* c_fallback_items  [] = { "None", "Sloppy" };

[[nodiscard]] auto to_lod_settings(const Lod_config& config) -> erhe::primitive::Lod_settings
{
    return erhe::primitive::Lod_settings{
        .level_count        = static_cast<std::size_t>(std::clamp(config.level_count, 1, static_cast<int>(erhe::primitive::max_lod_levels))),
        .ratio              = config.ratio,
        .attributes         = static_cast<erhe::primitive::Lod_attributes>(config.attributes),
        .normal_weight      = config.normal_weight,
        .color_weight       = config.color_weight,
        .border             = static_cast<erhe::primitive::Lod_border>(config.border),
        .seams              = static_cast<erhe::primitive::Lod_seams>(config.seams),
        .fallback           = static_cast<erhe::primitive::Lod_fallback>(config.fallback),
        .normal_merge_angle = config.normal_merge_angle
    };
}

// The view a pixel error is computed for: the last active viewport's camera
// position and projection scale (doc/plans/lod.md section 2.3).
class Pixel_view
{
public:
    bool      valid{false};
    bool      orthographic{false};
    glm::vec3 position{0.0f};
    float     projection_scale{0.0f};
};

[[nodiscard]] auto get_pixel_view(App_context& context, const Scene_root& scene_root) -> Pixel_view
{
    Pixel_view result{};
    if (context.scene_views == nullptr) {
        return result;
    }
    // The last viewport the pointer was in, when it shows this scene;
    // otherwise the first viewport window that does.
    std::shared_ptr<Viewport_scene_view> scene_view = context.scene_views->last_scene_view();
    if (!scene_view || (scene_view->get_scene_root().get() != &scene_root)) {
        scene_view.reset();
        for (const std::shared_ptr<Viewport_window>& viewport_window : context.scene_views->get_viewport_windows()) {
            std::shared_ptr<Viewport_scene_view> candidate = viewport_window->viewport_scene_view();
            if (candidate && (candidate->get_scene_root().get() == &scene_root) && candidate->get_camera()) {
                scene_view = std::move(candidate);
                break;
            }
        }
    }
    if (!scene_view) {
        return result;
    }
    const std::shared_ptr<erhe::scene::Camera> camera = scene_view->get_camera();
    if (!camera || (camera->projection() == nullptr)) {
        return result;
    }
    const erhe::math::Viewport viewport = scene_view->get_camera_viewport();
    if ((viewport.width <= 0) || (viewport.height <= 0)) {
        return result;
    }
    const erhe::scene::Projection&            projection = *camera->projection();
    const erhe::scene::Projection::Fov_sides  sides      = projection.get_fov_sides(viewport);
    const float                               height     = static_cast<float>(viewport.height);
    if (projection.is_orthographic()) {
        // Fov_sides of an orthographic projection are the view extents.
        const float view_height = sides.up - sides.down;
        if (!(view_height > 0.0f)) {
            return result;
        }
        result.orthographic     = true;
        result.projection_scale = height / view_height;
    } else {
        // height / (2 tan(fov_y / 2)) for a symmetric frustum.
        const float tan_span = std::tan(sides.up) - std::tan(sides.down);
        if (!(tan_span > 0.0f)) {
            return result;
        }
        result.projection_scale = height / tan_span;
    }
    result.position = glm::vec3{camera->position_in_world()};
    result.valid    = true;
    return result;
}

[[nodiscard]] auto get_max_axis_scale(const erhe::scene::Mesh& mesh) -> float
{
    const glm::mat4 world_from_node = mesh.world_from_node();
    return std::max(
        std::max(glm::length(glm::vec3{world_from_node[0]}), glm::length(glm::vec3{world_from_node[1]})),
        glm::length(glm::vec3{world_from_node[2]})
    );
}

} // anonymous namespace

Lod_window::Lod_window(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 app_context,
    App_message_bus&             app_message_bus,
    const Lod_config&            defaults
)
    : erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "LOD", "lod"}
    , m_context {app_context}
    , m_settings{to_lod_settings(defaults)}
{
    set_min_size(320.0f, 240.0f);

    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            on_close_scene(static_cast<const erhe::Item_host*>(message.scene_root.get()));
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

void Lod_window::on_close_scene(const erhe::Item_host* const closing_host)
{
    std::erase_if(
        m_edit_entries,
        [closing_host](const Edit_entry& entry) {
            return static_cast<const erhe::Item_host*>(entry.mesh->get_item_host()) == closing_host;
        }
    );
}

void Lod_window::on_items_removed(const Removed_items& removed)
{
    std::erase_if(
        m_edit_entries,
        [&removed](const Edit_entry& entry) {
            return removed.lookup.contains(entry.mesh.get());
        }
    );
}

// The meshes of the edit must still be content of a registered scene before
// the edit writes them; the subscriptions above drop them at the close or
// removal, and this check covers a host that left the registry another way.
void Lod_window::drop_dead_edit_entries()
{
    if (m_context.app_scenes == nullptr) {
        m_edit_entries.clear();
        return;
    }
    App_scenes& app_scenes = *m_context.app_scenes;
    std::erase_if(
        m_edit_entries,
        [&app_scenes](const Edit_entry& entry) {
            return !app_scenes.is_host_registered(entry.mesh->get_item_host());
        }
    );
}

void Lod_window::collect_selection(const Scene_root& scene_root)
{
    m_meshes.clear();
    if (m_context.selection == nullptr) {
        return;
    }
    for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_mesh(item);
        if (!mesh || (mesh->get_item_host() != &scene_root)) {
            continue;
        }
        if (std::find(m_meshes.begin(), m_meshes.end(), mesh) == m_meshes.end()) {
            m_meshes.push_back(std::move(mesh));
        }
    }
}

void Lod_window::imgui()
{
    ERHE_PROFILE_FUNCTION();

    const std::shared_ptr<Scene_root> scene_root = (m_context.selection != nullptr) ? m_context.selection->get_active_scene_root() : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        // The slider is not submitted: an edit in flight is over.
        m_level_editing = false;
        m_edit_entries.clear();
        ImGui::TextUnformatted("No active scene");
        return;
    }
    collect_selection(*scene_root);
    if (!m_level_editing) {
        m_edit_entries.clear();
    }

    if (ImGui::CollapsingHeader("Generation", ImGuiTreeNodeFlags_DefaultOpen)) {
        settings_imgui();
        generate_imgui(scene_root);
    }
    if (ImGui::CollapsingHeader("Forced level", ImGuiTreeNodeFlags_DefaultOpen)) {
        level_imgui();
    }
    if (ImGui::CollapsingHeader("Chains", ImGuiTreeNodeFlags_DefaultOpen)) {
        table_imgui(*scene_root);
    }

    // No reference outlives the draw (doc/editor/coding_rules.md
    // "Scene-hosted references in editor parts").
    m_meshes.clear();
    m_rows.clear();
}

void Lod_window::settings_imgui()
{
    int level_count = static_cast<int>(m_settings.level_count);
    if (ImGui::SliderInt("Levels", &level_count, 1, static_cast<int>(erhe::primitive::max_lod_levels))) {
        m_settings.level_count = static_cast<std::size_t>(std::clamp(level_count, 1, static_cast<int>(erhe::primitive::max_lod_levels)));
    }
    if (ImGui::SliderFloat("Ratio", &m_settings.ratio, 0.05f, 0.95f, "%.2f")) {
        m_settings.ratio = std::clamp(m_settings.ratio, 0.01f, 0.99f);
    }
    int attributes = static_cast<int>(m_settings.attributes);
    if (ImGui::Combo("Attributes", &attributes, c_attributes_items, IM_ARRAYSIZE(c_attributes_items))) {
        m_settings.attributes = static_cast<erhe::primitive::Lod_attributes>(attributes);
    }
    ImGui::BeginDisabled(m_settings.attributes == erhe::primitive::Lod_attributes::positions_only);
    if (ImGui::DragFloat("Normal Weight", &m_settings.normal_weight, 0.01f, 0.0f, 16.0f, "%.2f")) {
        m_settings.normal_weight = std::max(m_settings.normal_weight, 0.0f);
    }
    if (ImGui::DragFloat("Color Weight", &m_settings.color_weight, 0.01f, 0.0f, 16.0f, "%.2f")) {
        m_settings.color_weight = std::max(m_settings.color_weight, 0.0f);
    }
    ImGui::EndDisabled();
    int border = static_cast<int>(m_settings.border);
    if (ImGui::Combo("Border", &border, c_border_items, IM_ARRAYSIZE(c_border_items))) {
        m_settings.border = static_cast<erhe::primitive::Lod_border>(border);
    }
    int seams = static_cast<int>(m_settings.seams);
    if (ImGui::Combo("Seams", &seams, c_seams_items, IM_ARRAYSIZE(c_seams_items))) {
        m_settings.seams = static_cast<erhe::primitive::Lod_seams>(seams);
    }
    int fallback = static_cast<int>(m_settings.fallback);
    if (ImGui::Combo("Fallback", &fallback, c_fallback_items, IM_ARRAYSIZE(c_fallback_items))) {
        m_settings.fallback = static_cast<erhe::primitive::Lod_fallback>(fallback);
    }
    if (ImGui::SliderFloat("Normal Merge Angle", &m_settings.normal_merge_angle, 0.0f, 180.0f, "%.1f deg")) {
        m_settings.normal_merge_angle = std::clamp(m_settings.normal_merge_angle, 0.0f, 180.0f);
    }
}

void Lod_window::generate_imgui(const std::shared_ptr<Scene_root>& scene_root)
{
    static_cast<void>(scene_root);
    const bool has_meshes = !m_meshes.empty();
    ImGui::BeginDisabled(!has_meshes || (m_context.imgui_windows == nullptr));
    const bool generate = ImGui::Button("Generate for selection");
    ImGui::SameLine();
    const bool clear    = ImGui::Button("Clear for selection");
    ImGui::EndDisabled();
    if ((generate || clear) && has_meshes && (m_context.imgui_windows != nullptr)) {
        // ImGui-time work is queued (doc/editor/coding_rules.md): the
        // request runs at the start of the next prepare phase.
        std::vector<std::shared_ptr<erhe::Item_base>> items(m_meshes.begin(), m_meshes.end());
        const std::optional<erhe::primitive::Lod_settings> settings = generate
            ? std::optional<erhe::primitive::Lod_settings>{m_settings}
            : std::optional<erhe::primitive::Lod_settings>{};
        m_request_queued = true;
        m_context.imgui_windows->queue(
            [this, items = std::move(items), settings]() {
                const Lod_generation_result result = request_lod_generation(m_context, items, settings);
                m_request_queued = false;
                m_last_request = fmt::format(
                    "{}: {} meshes, {} primitives, {} queued, {} unchanged, {} held, {} not optimized",
                    settings.has_value() ? "Generate" : "Clear",
                    result.mesh_count, result.primitive_count, result.queued_count,
                    result.unchanged_count, result.held_count, result.not_optimized_count
                );
            }
        );
    }
    if (!has_meshes) {
        ImGui::TextUnformatted("Select meshes of the active scene");
    }
    const int pending = m_context.pending_async_ops.load();
    const int running = m_context.running_async_ops.load();
    if (m_request_queued) {
        ImGui::TextUnformatted("Request queued");
    } else if ((pending > 0) || (running > 0)) {
        ImGui::Text("Working: %d pending, %d running tasks", pending, running);
    } else if (!m_last_request.empty()) {
        ImGui::TextUnformatted("Idle");
    }
    if (!m_last_request.empty()) {
        ImGui::TextWrapped("%s", m_last_request.c_str());
    }
}

void Lod_window::level_imgui()
{
    if (m_meshes.empty()) {
        // The slider is not submitted: an edit in flight is over.
        m_level_editing = false;
        m_edit_entries.clear();
        ImGui::TextUnformatted("No meshes selected");
        return;
    }

    // The slider's range: the longest chain among the selected meshes.
    int max_level = 0;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_meshes) {
        for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
            const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
            if ((primitive != nullptr) && primitive->optimized_render_shape) {
                const erhe::primitive::Lod_chain& chain = primitive->optimized_render_shape->get_renderable_mesh().lod_chain;
                if (!chain.is_empty()) {
                    max_level = std::max(max_level, static_cast<int>(chain.level_count) - 1);
                }
            }
        }
    }

    // Shown value: the first selected mesh's forced level, while no edit runs.
    const erhe::scene::Lod_data record = erhe::scene::resolve_lod(*m_meshes.front());
    const bool forced = (record.mode == erhe::scene::Lod_mode::forced) && erhe::scene::carries_lod(*m_meshes.front());
    if (!m_level_editing) {
        m_level = static_cast<int>(erhe::scene::get_forced_lod_level(record));
    }
    const bool changed = ImGui::SliderInt("Level", &m_level, 0, std::max(max_level, 1));
    if (ImGui::IsItemActivated()) {
        begin_level_edit();
    }
    if (changed) {
        if (!m_level_editing) {
            begin_level_edit(); // keyboard edits can change without a separate activation frame
        }
        write_forced_level(m_level);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        end_level_edit();
    } else if (m_level_editing && ImGui::IsItemDeactivated()) {
        m_level_editing = false; // activated, never edited
        m_edit_entries.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Release")) {
        release_selection();
    }
    if (forced) {
        ImGui::Text("Forced to level %d", static_cast<int>(erhe::scene::get_forced_lod_level(record)));
    } else {
        ImGui::TextUnformatted("Not forced (draws level 0 until automatic selection)");
    }
}

void Lod_window::begin_level_edit()
{
    m_level_editing = true;
    m_edit_entries.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_meshes) {
        m_edit_entries.push_back(
            Edit_entry{
                .mesh         = mesh,
                .apply_lod    = mesh->read_local_state(erhe::scene::Lod::apply_lod_property.get()),
                .mode         = mesh->read_local_state(erhe::scene::Lod::mode_property.get()),
                .forced_level = mesh->read_local_state(erhe::scene::Lod::forced_level_property.get())
            }
        );
    }
}

void Lod_window::write_forced_level(const int level)
{
    // Live write while the slider is dragged, as the Properties window rows
    // do; end_level_edit() records the undoable operation.
    drop_dead_edit_entries();
    for (const Edit_entry& entry : m_edit_entries) {
        const std::shared_ptr<erhe::scene::Mesh>& mesh = entry.mesh;
        mesh->set_value(erhe::scene::Lod::apply_lod_property,    true);
        mesh->set_value(erhe::scene::Lod::mode_property,         erhe::scene::Lod_mode::forced);
        mesh->set_value(erhe::scene::Lod::forced_level_property, level);
    }
}

void Lod_window::end_level_edit()
{
    if (!m_level_editing) {
        return;
    }
    m_level_editing = false;
    drop_dead_edit_entries();
    if ((m_context.operation_stack != nullptr) && !m_edit_entries.empty()) {
        // One compound operation per completed edit: the after states are
        // the live-written local values; execute re-applies them
        // (idempotent) and undo restores the before states.
        Compound_operation::Parameters parameters;
        for (const Edit_entry& before : m_edit_entries) {
            const std::shared_ptr<erhe::scene::Mesh>& mesh = before.mesh;
            const erhe::property::Dependency_property& apply_lod    = erhe::scene::Lod::apply_lod_property.get();
            const erhe::property::Dependency_property& mode         = erhe::scene::Lod::mode_property.get();
            const erhe::property::Dependency_property& forced_level = erhe::scene::Lod::forced_level_property.get();
            parameters.operations.push_back(std::make_shared<Property_set_operation>(mesh, apply_lod,    before.apply_lod,    mesh->read_local_state(apply_lod)));
            parameters.operations.push_back(std::make_shared<Property_set_operation>(mesh, mode,         before.mode,         mesh->read_local_state(mode)));
            parameters.operations.push_back(std::make_shared<Property_set_operation>(mesh, forced_level, before.forced_level, mesh->read_local_state(forced_level)));
        }
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
    m_edit_entries.clear();
}

void Lod_window::release_selection()
{
    if ((m_context.operation_stack == nullptr) || m_meshes.empty()) {
        return;
    }
    // Clears the local Lod.apply_lod (default false) on each selected mesh;
    // the values stay, unused while the key is unset.
    const erhe::property::Dependency_property& apply_lod = erhe::scene::Lod::apply_lod_property.get();
    Compound_operation::Parameters parameters;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_meshes) {
        std::optional<erhe::property::Local_state> before = mesh->read_local_state(apply_lod);
        if (!before.has_value()) {
            continue;
        }
        parameters.operations.push_back(std::make_shared<Property_set_operation>(mesh, apply_lod, std::move(before), std::optional<erhe::property::Local_state>{}));
    }
    if (!parameters.operations.empty()) {
        m_context.operation_stack->queue(std::make_shared<Compound_operation>(std::move(parameters)));
    }
}

void Lod_window::table_imgui(Scene_root& scene_root)
{
    if (m_meshes.empty()) {
        return;
    }
    const Pixel_view pixel_view = get_pixel_view(m_context, scene_root);
    if (!pixel_view.valid) {
        ImGui::TextUnformatted("No viewport of this scene: pixel errors not shown");
    }

    // lod_settings, optimized_render_shape and the draw lists are main-thread
    // state the finalize commits write under the item host lock.
    const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root.item_host_mutex};

    m_rows.clear();
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : m_meshes) {
        const std::vector<erhe::scene::Mesh_primitive>& mesh_primitives = mesh->get_primitives();
        for (std::size_t index = 0, end = mesh_primitives.size(); index < end; ++index) {
            const erhe::primitive::Primitive* const primitive = mesh_primitives[index].primitive.get();
            if (primitive == nullptr) {
                continue;
            }
            const bool seen = std::any_of(m_rows.begin(), m_rows.end(), [primitive](const Primitive_row& row) { return row.primitive == primitive; });
            if (!seen) {
                m_rows.push_back(Primitive_row{.primitive = primitive, .mesh = mesh.get(), .mesh_primitive_index = index});
            }
        }
    }

    const erhe::scene_renderer::Draw_list_scene* const draw_list_scene = scene_root.get_draw_list_scene();
    for (std::size_t row_index = 0, row_end = m_rows.size(); row_index < row_end; ++row_index) {
        const Primitive_row&              row       = m_rows[row_index];
        const erhe::primitive::Primitive& primitive = *row.primitive;
        ImGui::PushID(static_cast<int>(row_index));
        ImGui::Text("%s (mesh %s, primitive %zu)", std::string{primitive.get_name()}.c_str(), row.mesh->get_name().c_str(), row.mesh_primitive_index);
        if (!primitive.optimized_render_shape) {
            ImGui::TextUnformatted("  no optimized variant (mesh optimization off or not built yet)");
            ImGui::PopID();
            continue;
        }
        const erhe::primitive::Buffer_mesh& buffer_mesh = primitive.optimized_render_shape->get_renderable_mesh();
        const erhe::primitive::Lod_chain&   chain       = buffer_mesh.lod_chain;
        if (chain.is_empty()) {
            ImGui::TextUnformatted(primitive.lod_settings.has_value() ? "  no chain (the mesh did not simplify)" : "  no chain");
        }

        // The level the mesh draws: its first color entry for this primitive.
        int drawn_level = -1;
        if (draw_list_scene != nullptr) {
            const erhe::scene_renderer::Draw_list_object* const object = draw_list_scene->get_object(draw_list_scene->find_object(row.mesh));
            if (object != nullptr) {
                for (const erhe::scene_renderer::Draw_list_entry_location& location : object->locations) {
                    const erhe::scene_renderer::Draw_list&       draw_list = draw_list_scene->get_draw_lists()[location.draw_list_index];
                    const erhe::scene_renderer::Draw_list_entry& entry     = draw_list.entries[location.entry_index];
                    if ((draw_list.key.purpose != erhe::scene_renderer::Draw_purpose::color) || (entry.mesh_primitive_index != row.mesh_primitive_index)) {
                        continue;
                    }
                    const erhe::primitive::Buffer_mesh* const entry_mesh = primitive.get_resolved_renderable_mesh(
                        entry.variant,
                        erhe::primitive::Primitive_mode::polygon_fill
                    ).second;
                    if (entry_mesh != nullptr) {
                        drawn_level = find_draw_list_entry_lod_level(entry, *entry_mesh);
                    }
                    break;
                }
            }
        }

        // Distance to the closest point of the mesh's world box; 0 inside.
        float      distance = 1.0f;
        bool       inside   = false;
        const float max_axis_scale = get_max_axis_scale(*row.mesh);
        if (pixel_view.valid && !pixel_view.orthographic) {
            const erhe::math::Aabb aabb = row.mesh->get_aabb_world();
            if (aabb.is_valid_3d()) {
                const glm::vec3 closest = glm::clamp(pixel_view.position, aabb.min, aabb.max);
                distance = glm::length(pixel_view.position - closest);
                inside   = !(distance > 0.0f);
            } else {
                distance = 0.0f;
                inside   = true;
            }
        }

        const std::size_t level_count = chain.is_empty() ? std::size_t{1} : chain.level_count;
        const std::size_t level_0_triangles = chain.is_empty() ? buffer_mesh.triangle_fill_indices.get_triangle_count() : chain.levels[0].index_range.get_triangle_count();
        const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
        if (ImGui::BeginTable("levels", 6, flags)) {
            ImGui::TableSetupColumn("Level");
            ImGui::TableSetupColumn("Triangles");
            ImGui::TableSetupColumn("Ratio");
            ImGui::TableSetupColumn("Error");
            ImGui::TableSetupColumn("Pixels");
            ImGui::TableSetupColumn("Drawn");
            ImGui::TableHeadersRow();
            for (std::size_t level = 0; level < level_count; ++level) {
                const std::size_t triangles = chain.is_empty() ? level_0_triangles : chain.levels[level].index_range.get_triangle_count();
                const float       error     = chain.is_empty() ? 0.0f : chain.levels[level].error;
                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("%zu", level);
                ImGui::TableNextColumn(); ImGui::Text("%zu", triangles);
                ImGui::TableNextColumn(); ImGui::Text("%.3f", (level_0_triangles > 0) ? (static_cast<double>(triangles) / static_cast<double>(level_0_triangles)) : 0.0);
                ImGui::TableNextColumn(); ImGui::Text("%.6f", error);
                ImGui::TableNextColumn();
                if (!pixel_view.valid) {
                    ImGui::TextUnformatted("-");
                } else if (inside) {
                    ImGui::TextUnformatted("inside");
                } else {
                    const float pixels = (error * max_axis_scale * pixel_view.projection_scale) / distance;
                    ImGui::Text("%.3f", pixels);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted((drawn_level == static_cast<int>(level)) ? "<" : "");
            }
            ImGui::EndTable();
        }
        if (pixel_view.valid && !pixel_view.orthographic && !inside) {
            ImGui::Text("Distance %.3f, scale %.3f", distance, max_axis_scale);
        }
        ImGui::PopID();
    }
}

} // namespace editor
