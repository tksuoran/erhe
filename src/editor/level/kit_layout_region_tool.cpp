#include "level/kit_layout_region_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "input_state.hpp"
#include "level/kit_layout_indicators.hpp"
#include "level/level_kits.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_scene/node.hpp"

namespace editor {

namespace {

// What the handle indices of the node's values name; nullopt for values that do not read.
[[nodiscard]] auto read_handle_set(const erhe::scene::Node& node) -> std::optional<Kit_layout_handle_set>
{
    Kit_layout_data data;
    if (!read_kit_layout(node, data).empty()) {
        return std::nullopt;
    }
    return get_layout_handle_set(data.pattern);
}

} // anonymous namespace

Kit_layout_region_tool::Kit_layout_region_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools
)
    : Point_set_tool{commands, context, app_message_bus, tools, "Kit_layout_region.select", "Kit_layout_region.cancel_edit"}
{
    set_description("Kit Layout Handle Edit");

    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message&) {
            m_target_stale = true;
            static_cast<void>(update_point_set());
        }
    );
    m_kit_layout_changed_subscription = app_message_bus.kit_layout_changed.subscribe(
        [this](Kit_layout_changed_message& message) {
            on_layout_changed(message.node_id);
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
            if (!layout) {
                return;
            }
            const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(layout.get());
            if (!scene_root || (scene_root == message.scene_root)) {
                set_target({});
            }
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
            if (layout && message.removed->lookup.contains(layout.get())) {
                set_target({});
            }
        }
    );
}

auto Kit_layout_region_tool::get_target_layout() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_target.lock();
}

auto Kit_layout_region_tool::get_selected_handle() const -> std::optional<std::size_t>
{
    return m_selected;
}

void Kit_layout_region_tool::set_target(const std::shared_ptr<erhe::scene::Node>& layout)
{
    if (layout == m_target.lock()) {
        return;
    }
    on_cancel_edit();
    m_target = layout;
    m_selected.reset();
    m_handle_set = layout ? read_handle_set(*layout) : std::nullopt;
}

void Kit_layout_region_tool::on_layout_changed(const uint64_t node_id)
{
    const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
    if (!layout || (static_cast<uint64_t>(layout->get_id()) != node_id) || is_own_edit_active()) {
        return;
    }
    // The handle indices name other handles once the kind, a line's count mode or a scatter's region changes.
    const std::optional<Kit_layout_handle_set> handle_set = read_handle_set(*layout);
    if (handle_set != m_handle_set) {
        m_selected.reset();
    }
    m_handle_set = handle_set;
}

auto Kit_layout_region_tool::update_point_set() -> bool
{
    if (m_target_stale && !is_own_edit_active()) {
        m_target_stale = false;
        std::shared_ptr<erhe::scene::Node> layout;
        if (m_context.selection != nullptr) {
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected = m_context.selection->get_selected_items();
            if (selected.size() == 1) {
                layout = std::dynamic_pointer_cast<erhe::scene::Node>(selected.front());
            }
        }
        if (layout && (!carries_kit_layout(*layout) || !get_hosting_scene_root(layout.get()))) {
            layout.reset();
        }
        set_target(layout);
    }
    return has_point_set_target();
}

auto Kit_layout_region_tool::has_point_set_target() const -> bool
{
    return !m_target.expired();
}

auto Kit_layout_region_tool::get_point_set_scene_root() const -> std::shared_ptr<Scene_root>
{
    const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
    return layout ? get_hosting_scene_root(layout.get()) : std::shared_ptr<Scene_root>{};
}

auto Kit_layout_region_tool::get_target_outline() const -> const Kit_layout_outline*
{
    const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
    if (!layout || (m_context.level_kits == nullptr) || !is_layout_scale_kept(layout->world_from_node())) {
        return nullptr;
    }
    const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(layout.get());
    if (!scene_root) {
        return nullptr;
    }
    return m_context.level_kits->get_layout_indicators().get_outline(*scene_root, *layout);
}

void Kit_layout_region_tool::get_handles(std::vector<Kit_region_handle>& out_handles) const
{
    out_handles.clear();
    const Kit_layout_outline* const outline = get_target_outline();
    if (outline != nullptr) {
        out_handles.insert(out_handles.end(), outline->region_handles.begin(), outline->region_handles.end());
    }
}

void Kit_layout_region_tool::get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const
{
    out_positions.clear();
    const Kit_layout_outline* const outline = get_target_outline();
    if (outline == nullptr) {
        return;
    }
    const glm::mat4 world_from_node = m_target.lock()->world_from_node();
    for (const Kit_region_handle& handle : outline->region_handles) {
        out_positions.push_back(glm::vec3{world_from_node * glm::vec4{handle.position, 1.0f}});
    }
}

auto Kit_layout_region_tool::is_point_selected(const std::size_t index) const -> bool
{
    return m_selected.has_value() && (m_selected.value() == index);
}

auto Kit_layout_region_tool::has_point_selection() const -> bool
{
    return has_point_set_target() && m_selected.has_value();
}

void Kit_layout_region_tool::select_point(const std::size_t index, const Point_select select)
{
    const Kit_layout_outline* const outline = get_target_outline();
    if ((outline == nullptr) || (index >= outline->region_handles.size())) {
        return;
    }
    // One handle at a time: two handles of one size would pull it two ways.
    if ((select == Point_select::toggle) && is_point_selected(index)) {
        m_selected.reset();
        return;
    }
    m_selected = index;
}

auto Kit_layout_region_tool::clear_point_selection() -> bool
{
    if (!m_selected.has_value()) {
        return false;
    }
    m_selected.reset();
    return true;
}

auto Kit_layout_region_tool::select_layout_handle(const std::shared_ptr<erhe::scene::Node>& layout, const std::optional<std::size_t> handle) -> bool
{
    if (!layout || (m_context.selection == nullptr)) {
        return false;
    }
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected = m_context.selection->get_selected_items();
    if ((selected.size() != 1) || (selected.front() != layout)) {
        m_context.selection->set_selection(std::vector<std::shared_ptr<erhe::Item_base>>{layout});
    }
    m_target_stale = true;
    static_cast<void>(update_point_set());
    if (m_target.lock() != layout) {
        return false;
    }
    if (!handle.has_value()) {
        m_selected.reset();
        return true;
    }
    const Kit_layout_outline* const outline = get_target_outline();
    if ((outline == nullptr) || (handle.value() >= outline->region_handles.size())) {
        return false;
    }
    m_selected = handle;
    return true;
}

auto Kit_layout_region_tool::get_point_frame_rotation() const -> glm::quat
{
    const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
    return layout ? layout->world_from_node_transform().get_rotation() : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

auto Kit_layout_region_tool::get_point_drawing() const -> Point_drawing
{
    return Point_drawing::all;
}

auto Kit_layout_region_tool::is_edit_lost() const -> bool
{
    const std::shared_ptr<Scene_root> scene_root = m_edit.scene_root.lock();
    return !m_edit.node || !scene_root || !m_edit.node->is_selected() || (get_hosting_scene_root(m_edit.node.get()) != scene_root);
}

void Kit_layout_region_tool::restore_start()
{
    Level_kits* const level_kits = m_context.level_kits;
    if ((level_kits == nullptr) || !m_edit.node) {
        return;
    }
    if (level_kits->is_layout_previewing(*m_edit.node)) {
        level_kits->end_layout_preview();
    }
    level_kits->get_layout_indicators().end_pending();
}

auto Kit_layout_region_tool::begin_point_edit() -> bool
{
    m_edit = Edit{};
    const std::shared_ptr<erhe::scene::Node> layout = m_target.lock();
    if (!layout || !m_selected.has_value() || (m_context.level_kits == nullptr)) {
        return false;
    }
    if (is_instance_structure_protected(*layout)) {
        log_tools->warn("Kit Layout Handle Edit refused: '{}' is inside a reference instance", layout->get_name());
        return false;
    }
    const Kit_layout_outline* const outline = get_target_outline();
    if ((outline == nullptr) || (m_selected.value() >= outline->region_handles.size())) {
        return false;
    }
    const std::string problem = read_kit_layout(*layout, m_edit.before);
    if (!problem.empty()) {
        log_tools->warn("Kit Layout Handle Edit refused: {}", problem);
        return false;
    }
    m_edit.node            = layout;
    m_edit.scene_root      = get_hosting_scene_root(layout.get());
    m_edit.handle          = outline->region_handles[m_selected.value()];
    m_edit.tried           = m_edit.before;
    m_edit.shown_pattern   = m_edit.before.pattern;
    m_edit.drag            = begin_layout_handle_drag(m_edit.before.pattern);
    m_edit.world_from_node = layout->world_from_node();
    m_edit.node_from_world = layout->node_from_world();
    m_edit.active          = true;
    return true;
}

void Kit_layout_region_tool::apply_point_edit(const std::optional<glm::mat4>& world_delta)
{
    if (!m_edit.active || m_edit.lost) {
        return;
    }
    if (is_edit_lost()) {
        restore_start();
        m_edit.lost = true;
        return;
    }
    if (!world_delta.has_value()) {
        if (m_edit.tried.pattern != m_edit.before.pattern) {
            restore_start();
            m_edit.tried.pattern = m_edit.before.pattern;
            m_edit.shown_pattern = m_edit.before.pattern;
        }
        m_edit.drag = begin_layout_handle_drag(m_edit.before.pattern);
        return;
    }
    // The handle moved by the gizmo's delta, in the node's frame from the press; the step's values follow from it.
    const glm::vec3 start_world = glm::vec3{m_edit.world_from_node * glm::vec4{m_edit.handle.position, 1.0f}};
    const glm::vec3 after_world = glm::vec3{world_delta.value() * glm::vec4{start_world, 1.0f}};
    const glm::vec3 after_local = glm::vec3{m_edit.node_from_world * glm::vec4{after_world, 1.0f}};
    const Kit_layout_pattern previous = m_edit.tried.pattern;
    m_edit.tried.pattern = m_edit.before.pattern;
    apply_region_handle(m_edit.handle, after_local, m_edit.drag, m_edit.tried.pattern);
    // A held drag updates every frame; only a change of the values is a step.
    if (m_edit.tried.pattern == previous) {
        return;
    }
    Level_kits& level_kits = *m_context.level_kits;
    if (m_edit.before.lock == Kit_layout_lock::locked) {
        Kit_layout_result result;
        const std::string refusal = level_kits.preview_layout_values(m_edit.node, m_edit.tried, result);
        if (refusal.empty()) {
            m_edit.shown_pattern = m_edit.tried.pattern;
        }
        return;
    }
    const std::shared_ptr<Scene_root> scene_root = m_edit.scene_root.lock();
    if (level_kits.get_layout_indicators().set_pending_values(*scene_root, *m_edit.node, m_edit.tried)) {
        m_edit.shown_pattern = m_edit.tried.pattern;
    }
}

void Kit_layout_region_tool::commit_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    if (m_edit.lost) {
        m_edit.node.reset();
        return;
    }
    const bool lost = is_edit_lost();
    // The layout gets its children and values from before the drag back; set_layout() records the step shown last.
    restore_start();
    const std::shared_ptr<erhe::scene::Node> layout     = std::move(m_edit.node);
    const std::shared_ptr<Scene_root>        scene_root = m_edit.scene_root.lock();
    if (lost || !scene_root || (m_edit.shown_pattern == m_edit.before.pattern)) {
        return;
    }
    Kit_layout_data data = m_edit.before;
    data.pattern = m_edit.shown_pattern;
    const Kit_layout_result result = m_context.level_kits->set_layout(*scene_root, layout, data);
    if (!result.error.empty()) {
        log_tools->warn("Kit Layout Handle Edit of '{}' refused: {}", layout->get_name(), result.error);
    }
}

void Kit_layout_region_tool::cancel_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    if (!m_edit.lost) {
        restore_start();
    }
    m_edit.node.reset();
}

auto Kit_layout_region_tool::try_ready_miss(Viewport_scene_view&, const glm::vec2&) -> bool
{
    // A plain click off the handles clears the handle first; the next one reaches the object selection.
    const Input_state* const input_state = m_context.input_state;
    const bool modifier = (input_state != nullptr) && (input_state->control || input_state->shift || input_state->alt);
    m_miss_clear = !modifier && has_point_selection();
    return m_miss_clear;
}

auto Kit_layout_region_tool::on_select_miss() -> bool
{
    const bool clear = m_miss_clear;
    m_miss_clear = false;
    return clear && clear_point_selection();
}

}
