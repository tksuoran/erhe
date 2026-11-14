#include "level/kit_layout_editor.hpp"

#include "level/kit_parameter_rows.hpp"
#include "level/kit_shape_library.hpp"
#include "level/level_kits.hpp"
#include "level/level_queries.hpp"

#include "app_context.hpp"
#include "app_scenes.hpp"
#include "editor_log.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_imgui/imgui_item_recorder.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_math/aabb.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <fmt/format.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <optional>

namespace editor {

namespace {

void tooltip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("%s", text);
    }
}

// True when the last item's edit ended with a change: a drag released or a value typed.
[[nodiscard]] auto edit_ended() -> bool
{
    return ImGui::IsItemDeactivatedAfterEdit();
}

// A three component drag whose components are recorded as '<label>.x', '.y' and '.z'; returns true when an edit ended.
[[nodiscard]] auto vec3_row(const char* label, glm::vec3& value, const float min_value, const float max_value, const char* format) -> bool
{
    const std::size_t first_item = erhe::imgui::get_recorded_item_count();
    ImGui::DragFloat3(label, &value.x, 0.01f, min_value, max_value, format);
    const bool ended = edit_ended();
    if (erhe::imgui::is_item_recording()) {
        erhe::imgui::set_recorded_item_labels(first_item, label);
    }
    return ended;
}

// The rows of a pattern (R5.12.1, R5.12.7); returns true when an edit ended this frame.
[[nodiscard]] auto pattern_rows(Kit_layout_pattern& pattern) -> bool
{
    bool ended = false;
    int kind = static_cast<int>(pattern.kind);
    ended = ImGui::RadioButton("Line", &kind, static_cast<int>(Repeat_pattern_kind::line)) || ended;
    tooltip("Pieces along a direction from the layout node's origin.");
    ImGui::SameLine();
    ended = ImGui::RadioButton("Grid", &kind, static_cast<int>(Repeat_pattern_kind::grid)) || ended;
    tooltip("Count X by Count Z pieces along the node's x and z axes.");
    ImGui::SameLine();
    ended = ImGui::RadioButton("Ring", &kind, static_cast<int>(Repeat_pattern_kind::ring)) || ended;
    tooltip("Pieces on a ring about the node's up axis, its center Radius behind the node's front.");
    ImGui::SameLine();
    ended = ImGui::RadioButton("Polyline", &kind, static_cast<int>(Repeat_pattern_kind::polyline)) || ended;
    tooltip("Pieces along the path from the node's origin through the points, x along the path.");
    pattern.kind = static_cast<Repeat_pattern_kind>(kind);

    const int max_count = static_cast<int>(c_max_repeat_count);
    if (pattern.kind != Repeat_pattern_kind::grid) {
        int mode = static_cast<int>(pattern.count_mode);
        ended = ImGui::RadioButton("By Count", &mode, static_cast<int>(Kit_layout_count_mode::count)) || ended;
        tooltip("The piece count is stated.");
        ImGui::SameLine();
        ended = ImGui::RadioButton("By Gap", &mode, static_cast<int>(Kit_layout_count_mode::gap)) || ended;
        tooltip("The free distance between neighbouring pieces is stated; the space of the arrangement gives the count.");
        pattern.count_mode = static_cast<Kit_layout_count_mode>(mode);
        if (pattern.count_mode == Kit_layout_count_mode::count) {
            ImGui::DragInt("Count", &pattern.count, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            ended = edit_ended() || ended;
            tooltip("Pieces in the run.");
        } else {
            ImGui::DragFloat("Gap", &pattern.gap, 0.01f, -100.0f, 100.0f, "%.3f m");
            ended = edit_ended() || ended;
            tooltip("The least free distance between neighbouring pieces; the remainder of the space spreads over the gaps.");
        }
    }
    switch (pattern.kind) {
        case Repeat_pattern_kind::line: {
            ended = vec3_row("Direction", pattern.direction, -1.0f, 1.0f, "%.3f") || ended;
            tooltip("The line's direction in the node's frame.");
            if (pattern.count_mode == Kit_layout_count_mode::count) {
                ImGui::DragFloat("Step", &pattern.step, 0.01f, 0.0f, 1000.0f, "%.3f m");
                ended = edit_ended() || ended;
                tooltip("Center to center; by default the piece's width along x.");
            } else {
                ImGui::DragFloat("Length", &pattern.length, 0.01f, 0.0f, 10000.0f, "%.3f m");
                ended = edit_ended() || ended;
                tooltip("From the first piece's center to the last one's.");
            }
            break;
        }
        case Repeat_pattern_kind::grid: {
            ImGui::DragInt("Count X", &pattern.count_x, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            ended = edit_ended() || ended;
            ImGui::DragInt("Count Z", &pattern.count_z, 0.1f, 1, max_count, "%d", ImGuiSliderFlags_AlwaysClamp);
            ended = edit_ended() || ended;
            tooltip("Pieces along the node's x and z axes.");
            ImGui::DragFloat("Spacing X", &pattern.spacing_x, 0.01f, -1000.0f, 1000.0f, "%.3f m");
            ended = edit_ended() || ended;
            ImGui::DragFloat("Spacing Z", &pattern.spacing_z, 0.01f, -1000.0f, 1000.0f, "%.3f m");
            ended = edit_ended() || ended;
            tooltip("The step along the node's x and z axes; by default the piece's extent along them.");
            break;
        }
        case Repeat_pattern_kind::ring: {
            ImGui::DragFloat("Radius", &pattern.radius, 0.01f, 0.01f, 1000.0f, "%.3f m", ImGuiSliderFlags_AlwaysClamp);
            ended = edit_ended() || ended;
            ImGui::DragFloat("Arc", &pattern.arc_degrees, 0.5f, 1.0f, 360.0f, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
            ended = edit_ended() || ended;
            tooltip("360 is a full ring; a smaller arc has pieces at both of its ends.");
            bool outward = (pattern.facing == Repeat_ring_facing::outward);
            if (ImGui::Checkbox("Face Outward", &outward)) {
                pattern.facing = outward ? Repeat_ring_facing::outward : Repeat_ring_facing::start_rotation;
                ended = true;
            }
            tooltip("Each piece turns with the ring; off, each keeps the node's rotation.");
            break;
        }
        case Repeat_pattern_kind::polyline: {
            ImGui::Text("Path: the origin and %zu points (node frame)", pattern.points.size());
            std::optional<std::size_t> removed;
            for (std::size_t i = 0, end = pattern.points.size(); i < end; ++i) {
                ImGui::PushID(static_cast<int>(i));
                ended = vec3_row("Point", pattern.points[i], -10000.0f, 10000.0f, "%.3f m") || ended;
                tooltip("A point of the path in the layout node's frame.");
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Point")) {
                    removed = i;
                }
                ImGui::PopID();
            }
            if (removed.has_value()) {
                pattern.points.erase(pattern.points.begin() + static_cast<std::ptrdiff_t>(removed.value()));
                ended = true;
            }
            if (ImGui::Button("Add Point")) {
                // The new point continues the last segment, else steps along x from the origin.
                const glm::vec3 last     = pattern.points.empty() ? glm::vec3{0.0f} : pattern.points.back();
                const glm::vec3 previous = (pattern.points.size() >= 2) ? pattern.points[pattern.points.size() - 2] : glm::vec3{0.0f};
                const glm::vec3 segment  = (pattern.points.empty() || (glm::length(last - previous) <= 0.0f)) ? glm::vec3{std::max(pattern.step, 1.0f) * 3.0f, 0.0f, 0.0f} : (last - previous);
                pattern.points.push_back(last + segment);
                ended = true;
            }
            tooltip("Adds a point after the last one, continuing its segment.");
            break;
        }
        default: {
            break;
        }
    }
    return ended;
}

[[nodiscard]] auto to_surface_snap  (const bool on) -> Surface_snap     { return on ? Surface_snap::on : Surface_snap::off; }
[[nodiscard]] auto to_surface_orient(const bool on) -> Surface_orient   { return on ? Surface_orient::on : Surface_orient::off; }

} // anonymous namespace

Kit_layout_editor::Kit_layout_editor(App_context& context)
    : m_context{context}
{
}

void Kit_layout_editor::mark_dirty()
{
    m_dirty = true;
}

void Kit_layout_editor::mark_geometry_changed()
{
    m_new_piece.reset();
    m_dirty = true;
}

void Kit_layout_editor::imgui(
    const std::shared_ptr<Scene_root>&        scene_root,
    const std::shared_ptr<erhe::scene::Node>& layout,
    const std::shared_ptr<erhe::scene::Mesh>& piece,
    const Kit_catalog_item&                   catalog_item
)
{
    ImGui::PushID("layout");
    if (layout) {
        layout_imgui(scene_root, layout, piece, catalog_item);
    } else if (piece) {
        make_imgui(scene_root, piece);
    } else {
        ImGui::TextWrapped("Select a kit piece to make a kit layout node from it, or a kit layout node to edit it.");
    }
    message_imgui();
    ImGui::PopID();
}

void Kit_layout_editor::message_imgui() const
{
    if (m_failed) {
        kit_message_line(m_message);
    } else if (!m_message.empty()) {
        ImGui::TextWrapped("%s", m_message.c_str());
    }
}

void Kit_layout_editor::make_imgui(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece)
{
    // The step and spacings follow the piece's extent when another piece is chosen or a mesh changed (message).
    if (m_new_piece.lock() != piece) {
        m_new_piece   = piece;
        m_new_pattern = Kit_layout_pattern{};
        const erhe::math::Aabb bounds = mesh_local_bounds(*piece);
        const glm::vec3 extent = bounds.is_valid_3d() ? (bounds.max - bounds.min) : glm::vec3{1.0f};
        set_layout_spacing_from_extent(m_new_pattern, extent);
        m_new_pattern.points.push_back(glm::vec3{3.0f * m_new_pattern.step, 0.0f, 0.0f});
    }
    ImGui::Text("Piece: %s", piece->get_name().c_str());
    static_cast<void>(pattern_rows(m_new_pattern));
    if (ImGui::Button("Make Layout")) {
        queue_make(scene_root, piece);
    }
    tooltip(
        "Makes a kit layout node in the piece's place with the piece's shape, values and slot materials as its item, the pattern above and "
        "the placement settings' Snap To Module and surface pair, locked; the piece becomes its element 0. One undo step; selects the node."
    );
}

void Kit_layout_editor::layout_imgui(
    const std::shared_ptr<Scene_root>&        scene_root,
    const std::shared_ptr<erhe::scene::Node>& layout,
    const std::shared_ptr<erhe::scene::Mesh>& piece,
    const Kit_catalog_item&                   catalog_item
)
{
    if (m_layout.lock() != layout) {
        m_layout = layout;
        m_ready  = false;
        m_dirty  = true;
    }
    // Read again only on messages or another node, never while one of the rows is being edited.
    if (m_dirty && !m_read_queued && !ImGui::IsAnyItemActive()) {
        queue_read(scene_root, layout);
    }
    ImGui::Text("Layout: %s (%zu pieces)", layout->get_name().c_str(), layout->get_child_count());
    if (!m_ready) {
        return;
    }
    if (!m_problem.empty()) {
        kit_message_line(m_problem);
        return;
    }
    const bool locked = (m_data.lock == Kit_layout_lock::locked);
    ImGui::TextWrapped(
        "%s",
        locked
            ? "Locked: the layout makes its children from these values."
            : "Unlocked: the children are edited directly; these rows change the values alone."
    );

    if (pattern_rows(m_data.pattern)) {
        queue_set(scene_root, layout, m_data);
    }
    if (!m_run_problem.empty()) {
        kit_message_line(m_run_problem);
    } else if (m_data.pattern.kind == Repeat_pattern_kind::grid) {
        ImGui::Text("Count %zu", m_run.count);
    } else if (m_data.pattern.count_mode == Kit_layout_count_mode::gap) {
        ImGui::Text("Resolved count %zu, actual gap %.3f m", m_run.count, (m_run.count > 1) ? (m_run.step - m_item_extent) : 0.0f);
        tooltip("The count the space holds at the stated gap, and the gap once the remainder is spread.");
    } else {
        ImGui::Text("Count %zu, step %.3f m", m_run.count, m_run.step);
    }

    items_imgui(scene_root, layout, piece, catalog_item);

    ImGui::SeparatorText("Snap And Seeds");
    bool module_snap    = (m_data.snap == Kit_snap::module);
    bool surface_snap   = (m_data.surface_snap == Surface_snap::on);
    bool surface_orient = (m_data.surface_orient == Surface_orient::on);
    bool random_seeds   = (m_data.seeds == Kit_layout_seeds::per_element);
    bool ended = false;
    if (ImGui::Checkbox("Module Snap", &module_snap)) {
        m_data.snap = module_snap ? Kit_snap::module : Kit_snap::none;
        ended = true;
    }
    tooltip("Each piece's position rounds to multiples of the scene's module.");
    if (ImGui::Checkbox("Surface Snap", &surface_snap)) {
        m_data.surface_snap = to_surface_snap(surface_snap);
        ended = true;
    }
    tooltip("Each piece rests on the surface under it.");
    if (ImGui::Checkbox("Surface Orient", &surface_orient)) {
        m_data.surface_orient = to_surface_orient(surface_orient);
        ended = true;
    }
    tooltip("Each piece's up axis takes the normal of the surface under it.");
    if (ImGui::Checkbox("Random Seeds", &random_seeds)) {
        m_data.seeds = random_seeds ? Kit_layout_seeds::per_element : Kit_layout_seeds::item;
        ended = true;
    }
    tooltip("Each piece takes a seed of its own from the layout seed and its index; its item's seed mask derives from it.");
    ImGui::DragInt("Layout Seed", &m_data.seed, 1.0f, 0, kit_seed_max, "%d", ImGuiSliderFlags_AlwaysClamp);
    ended = edit_ended() || ended;
    tooltip("The seed the per-piece seeds come from.");
    if (ended) {
        queue_set(scene_root, layout, m_data);
    }

    ImGui::SeparatorText("Lock");
    ImGui::BeginDisabled(locked);
    if (ImGui::Button("Lock")) {
        queue_lock(scene_root, layout, Kit_layout_lock::locked);
    }
    ImGui::EndDisabled();
    tooltip("Locks the layout: every child is replaced by the pieces its values make, which DISCARDS the edits made to the children while unlocked. One undo step.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!locked);
    if (ImGui::Button("Unlock")) {
        queue_lock(scene_root, layout, Kit_layout_lock::unlocked);
    }
    tooltip("Unlocks the layout: no child changes, and the children are then edited directly. One undo step.");
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        queue_refresh(scene_root, layout);
    }
    tooltip("Makes the children equal to the values again, keeping the ones that match (a new module, a changed surface below). One undo step when anything changes.");
    ImGui::EndDisabled();
}

void Kit_layout_editor::items_imgui(
    const std::shared_ptr<Scene_root>&        scene_root,
    const std::shared_ptr<erhe::scene::Node>& layout,
    const std::shared_ptr<erhe::scene::Mesh>& piece,
    const Kit_catalog_item&                   catalog_item
)
{
    ImGui::SeparatorText("Items");
    const Kit_shape_library& library = m_context.level_kits->get_library();
    std::optional<std::size_t> removed;
    for (std::size_t i = 0, end = m_items.size(); i < end; ++i) {
        Kit_layout_item_rows& rows = m_items[i];
        ImGui::PushID(static_cast<int>(i));
        const Kit_shape_definition* const shape = rows.problem.empty() ? rows.values.get_shape() : nullptr;
        ImGui::Text("Item %zu: %s", i + 1, (shape != nullptr) ? shape->display_name.c_str() : m_data.items[i].shape.c_str());
        if (shape == nullptr) {
            kit_message_line(rows.problem);
        } else {
            const Kit_row_edit edit = kit_parameter_rows(rows.values, m_lengths);
            if (edit == Kit_row_edit::committed) {
                rows.message = rows.values.validate();
                if (rows.message.empty()) {
                    Kit_layout_data data = m_data;
                    data.items[i].parameters = rows.values.to_canonical_text();
                    queue_set(scene_root, layout, data);
                }
            } else if (edit == Kit_row_edit::cancelled) {
                m_dirty = true;
            }
            std::string chosen;
            const std::optional<std::size_t> slot = slot_material_combos(
                library, *shape, [&rows](const std::size_t j) -> const std::string& { return rows.materials[j]; }, chosen
            );
            if (slot.has_value()) {
                Kit_layout_data data = m_data;
                data.items[i].materials.resize(shape->material_slots.size());
                data.items[i].materials[slot.value()] = chosen;
                queue_set(scene_root, layout, data);
            }
            kit_stairs_line(rows.values);
            kit_message_line(rows.message);
        }
        ImGui::BeginDisabled(m_items.size() < 2);
        if (ImGui::Button("Remove Item")) {
            removed = i;
        }
        ImGui::EndDisabled();
        tooltip("Removes this item from the layout; a layout keeps at least one item.");
        ImGui::PopID();
    }
    if (removed.has_value()) {
        Kit_layout_data data = m_data;
        data.items.erase(data.items.begin() + static_cast<std::ptrdiff_t>(removed.value()));
        queue_set(scene_root, layout, data);
    }

    ImGui::BeginDisabled(catalog_item.shape == nullptr);
    if (ImGui::Button("Add Shape Item")) {
        Kit_layout_data data = m_data;
        Kit_layout_item item;
        item.shape      = catalog_item.shape->id;
        item.parameters = catalog_item.values->to_canonical_text();
        item.materials.assign(catalog_item.slot_materials.begin(), catalog_item.slot_materials.end());
        data.items.push_back(std::move(item));
        queue_set(scene_root, layout, data);
    }
    ImGui::EndDisabled();
    tooltip("Adds the shape chosen above, with its parameter rows' values and slot materials, as the layout's next item; element i takes item i modulo the item count.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!piece);
    if (ImGui::Button("Add Piece Item")) {
        Kit_layout_data data = m_data;
        Kit_layout_item item;
        const std::string error = m_context.level_kits->read_piece_layout_item(*piece, item);
        if (error.empty()) {
            data.items.push_back(std::move(item));
            queue_set(scene_root, layout, data);
        } else {
            m_failed  = true;
            m_message = error;
        }
    }
    ImGui::EndDisabled();
    tooltip("Adds the selected kit piece's shape, values and slot materials as the layout's next item.");
}

void Kit_layout_editor::read_layout(Scene_root& scene_root, const erhe::scene::Node& layout)
{
    Level_kits& level_kits = *m_context.level_kits;
    m_ready   = true;
    m_dirty   = false;
    m_problem = read_kit_layout(layout, m_data);
    m_items.clear();
    m_run = Kit_layout_run{};
    m_run_problem.clear();
    m_item_extent = 0.0f;
    if (!m_problem.empty()) {
        return;
    }
    m_lengths = level_kits.get_lengths(scene_root);
    const Kit_shape_library& library = level_kits.get_library();
    for (const Kit_layout_item& item : m_data.items) {
        Kit_layout_item_rows& rows = m_items.emplace_back();
        Kit_placement placement;
        rows.problem = level_kits.read_layout_item(scene_root, item, placement);
        if (!rows.problem.empty()) {
            continue;
        }
        rows.values = placement.values.value();
        for (std::size_t slot = 0, end = placement.shape->material_slots.size(); slot < end; ++slot) {
            const Kit_palette_material* const fallback = library.find_slot_default(placement.shape->material_slots[slot]);
            const bool given = (slot < item.materials.size()) && !item.materials[slot].empty();
            rows.materials.push_back(given ? item.materials[slot] : ((fallback != nullptr) ? fallback->name : std::string{}));
        }
        const erhe::math::Aabb bounds = level_kits.get_layout_item_bounds(scene_root, item);
        if (bounds.is_valid_3d()) {
            m_item_extent = std::max(m_item_extent, bounds.max.x - bounds.min.x);
        }
    }
    m_run_problem = resolve_layout_pattern(m_data.pattern, m_item_extent, m_run);
}

void Kit_layout_editor::queue_read(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout)
{
    m_read_queued = true;
    // Prepare phase: an item's brush may be made here and upload its mesh.
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_layout = std::weak_ptr<erhe::scene::Node>{layout}]() {
            m_read_queued = false;
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get()) || (node != m_layout.lock())) {
                return;
            }
            read_layout(*target, *node);
        }
    );
}

void Kit_layout_editor::queue_set(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, const Kit_layout_data& data)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_layout = std::weak_ptr<erhe::scene::Node>{layout}, data]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->set_layout(*target, node, data);
            m_failed  = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Level Kit: layout '{}' edit refused: {}", node->get_name(), result.error);
            }
            m_message = m_failed
                ? result.error
                : (result.changed ? fmt::format("Set the values of '{}': {} pieces kept, {} added, {} removed.", node->get_name(), result.kept_count, result.added_count, result.removed_count) : std::string{});
            // A refused or unchanged edit shows the node's values again.
            m_dirty = true;
        }
    );
}

void Kit_layout_editor::queue_lock(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout, const Kit_layout_lock lock)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_layout = std::weak_ptr<erhe::scene::Node>{layout}, lock]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->set_layout_lock(*target, node, lock);
            m_failed  = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Level Kit: layout '{}' lock refused: {}", node->get_name(), result.error);
                m_message = result.error;
            } else if (lock == Kit_layout_lock::locked) {
                m_message = fmt::format("Locked '{}': {} pieces made.", node->get_name(), result.added_count);
            } else {
                m_message = fmt::format("Unlocked '{}'.", node->get_name());
            }
            m_dirty = true;
        }
    );
}

void Kit_layout_editor::queue_refresh(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Node>& layout)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_layout = std::weak_ptr<erhe::scene::Node>{layout}]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Node> node   = weak_layout.lock();
            if (!target || !node || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            const Kit_layout_result result = m_context.level_kits->refresh_layout(*target, node);
            m_failed  = !result.error.empty();
            if (m_failed) {
                log_scene->warn("Level Kit: layout '{}' refresh refused: {}", node->get_name(), result.error);
            }
            m_message = m_failed
                ? result.error
                : (result.changed
                    ? fmt::format("Refreshed '{}': {} pieces kept, {} added, {} removed.", node->get_name(), result.kept_count, result.added_count, result.removed_count)
                    : fmt::format("'{}' already matches its values.", node->get_name()));
            m_dirty = true;
        }
    );
}

void Kit_layout_editor::queue_make(const std::shared_ptr<Scene_root>& scene_root, const std::shared_ptr<erhe::scene::Mesh>& piece)
{
    m_context.imgui_windows->queue(
        [this, weak_scene_root = std::weak_ptr<Scene_root>{scene_root}, weak_piece = std::weak_ptr<erhe::scene::Mesh>{piece}, pattern = m_new_pattern]() {
            const std::shared_ptr<Scene_root>        target = weak_scene_root.lock();
            const std::shared_ptr<erhe::scene::Mesh> mesh   = weak_piece.lock();
            if (!target || !mesh || !m_context.app_scenes->is_host_registered(target.get())) {
                return;
            }
            Level_kits& level_kits = *m_context.level_kits;
            const Kit_placement_settings settings = level_kits.get_placement_settings();
            const Kit_layout_result result = level_kits.make_layout_from_piece(
                *target,
                Kit_layout_from_piece{
                    .piece          = mesh,
                    .pattern        = pattern,
                    .snap           = settings.snap,
                    .surface_snap   = settings.surface.snap,
                    .surface_orient = settings.surface.orient,
                    .seeds          = Kit_layout_seeds::item,
                    .seed           = 0,
                    .lock           = Kit_layout_lock::locked,
                    .name           = {}
                }
            );
            m_failed  = !result.error.empty();
            m_message = m_failed ? result.error : fmt::format("Made '{}' with {} pieces.", result.node->get_name(), result.added_count);
            if (m_failed) {
                log_scene->warn("Level Kit: layout from '{}' refused: {}", mesh->get_name(), result.error);
            }
            if (!m_failed) {
                m_context.selection->set_selection({result.node}, result.node);
            }
        }
    );
}

} // namespace editor
