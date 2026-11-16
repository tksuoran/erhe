#include "tools/curve_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "editor_log.hpp"
#include "input_state.hpp"
#include "level/kit_layout.hpp"
#include "level/level_kits.hpp"
#include "level/level_repeat.hpp"
#include "operations/curve_set_operation.hpp"
#include "operations/operation_stack.hpp"
#include "prefabs/instance_structure.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"

#include "config/generated/editor_settings_config.hpp"
#include "renderers/render_context.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/curve.hpp"

#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <limits>

namespace editor {

using erhe::math::Curve_handle;
using erhe::math::Curve_path;
using erhe::math::Curve_point;
using erhe::math::Curve_point_kind;

namespace {

[[nodiscard]] auto paths_equal(const Curve_path& a, const Curve_path& b) -> bool
{
    if ((a.closure != b.closure) || (a.points.size() != b.points.size())) {
        return false;
    }
    for (std::size_t i = 0, end = a.points.size(); i < end; ++i) {
        const Curve_point& p = a.points[i];
        const Curve_point& q = b.points[i];
        if ((p.position != q.position) || (p.in_handle != q.in_handle) || (p.out_handle != q.out_handle) || (p.kind != q.kind)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] auto edit_refusal(const erhe::scene::Curve& curve) -> std::string
{
    if (is_instance_structure_protected(curve)) {
        return fmt::format("curve '{}' is inside a reference instance; its path is not an override", curve.get_name());
    }
    return {};
}

[[nodiscard]] auto make_operation(const std::shared_ptr<erhe::scene::Curve>& curve, Curve_path after, std::string what) -> std::shared_ptr<Curve_set_operation>
{
    return std::make_shared<Curve_set_operation>(curve, curve->get_path(), std::move(after), std::move(what));
}

// The node frame of a segment handle: x the segment's normal, y the node's up axis.
[[nodiscard]] auto segment_frame_rotation(const glm::vec3& normal) -> glm::quat
{
    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    return glm::quat_cast(glm::mat3{normal, up, glm::cross(normal, up)});
}

[[nodiscard]] auto is_segments_layout(const erhe::scene::Curve& curve) -> bool
{
    if (!carries_kit_layout(curve)) {
        return false;
    }
    Kit_layout_data data;
    return read_kit_layout(curve, data).empty() && (data.pattern.kind == Repeat_pattern_kind::segments);
}

[[nodiscard]] auto part_of(const std::size_t index) -> Curve_point_part
{
    return static_cast<Curve_point_part>(index % Curve_tool::parts_per_point);
}

[[nodiscard]] auto index_of(const std::size_t point, const Curve_point_part part) -> std::size_t
{
    return (point * Curve_tool::parts_per_point) + static_cast<std::size_t>(part);
}

} // anonymous namespace

auto c_str(const Curve_point_part part) -> const char*
{
    switch (part) {
        case Curve_point_part::position:   return "point";
        case Curve_point_part::in_handle:  return "in";
        case Curve_point_part::out_handle: return "out";
        default:                           return "?";
    }
}

auto curve_point_part_from_string(const std::string_view text, Curve_point_part& out_part) -> bool
{
    for (const Curve_point_part part : {Curve_point_part::position, Curve_point_part::in_handle, Curve_point_part::out_handle}) {
        if (text == c_str(part)) {
            out_part = part;
            return true;
        }
    }
    return false;
}

auto make_curve_add_point_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const erhe::math::Curve_end                end,
    const std::optional<Curve_point>&          point,
    std::size_t&                               out_index,
    std::string&                               out_error
) -> std::shared_ptr<Curve_set_operation>
{
    out_error = edit_refusal(*curve);
    if (!out_error.empty()) {
        return {};
    }
    if (curve->get_path().closure == erhe::math::Curve_closure::closed) {
        out_error = "a closed curve has no ends; insert a point on the curve instead";
        return {};
    }
    Curve_path after = curve->get_path();
    const Curve_point added = point.has_value() ? point.value() : erhe::math::make_curve_end_point(after, end);
    out_index = erhe::math::add_curve_point(after, end, added);
    return make_operation(curve, std::move(after), (end == erhe::math::Curve_end::start) ? "add point at start" : "add point at end");
}

auto make_curve_insert_point_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const erhe::math::Curve_location&          location,
    std::size_t&                               out_index,
    std::string&                               out_error
) -> std::shared_ptr<Curve_set_operation>
{
    out_error = edit_refusal(*curve);
    if (!out_error.empty()) {
        return {};
    }
    Curve_path after = curve->get_path();
    if (location.span >= erhe::math::get_span_count(after)) {
        out_error = fmt::format("span {} is outside the curve's {} spans", location.span, erhe::math::get_span_count(after));
        return {};
    }
    constexpr float c_end_margin = 1.0e-4f;
    if (!(location.t > c_end_margin) || !(location.t < (1.0f - c_end_margin))) {
        out_error = "the place is at a control point; a point is inserted inside a span";
        return {};
    }
    out_index = erhe::math::insert_curve_point(after, location);
    return make_operation(curve, std::move(after), fmt::format("insert point {}", out_index));
}

auto make_curve_remove_points_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const std::span<const std::size_t>         indices,
    std::string&                               out_error
) -> std::shared_ptr<Curve_set_operation>
{
    out_error = edit_refusal(*curve);
    if (!out_error.empty()) {
        return {};
    }
    const Curve_path& before = curve->get_path();
    std::vector<bool> removed(before.points.size(), false);
    std::size_t       count = 0;
    for (const std::size_t index : indices) {
        if (index >= before.points.size()) {
            out_error = fmt::format("index {} is outside [0, {}]", index, before.points.size() - 1);
            return {};
        }
        if (removed[index]) {
            out_error = fmt::format("index {} is given twice", index);
            return {};
        }
        removed[index] = true;
        ++count;
    }
    if (count == 0) {
        out_error = "no points to remove";
        return {};
    }
    if ((before.points.size() - count) < 2) {
        out_error = fmt::format("a curve keeps at least 2 points; removing {} of {} would leave {}", count, before.points.size(), before.points.size() - count);
        return {};
    }
    Curve_path after;
    after.closure = before.closure;
    for (std::size_t i = 0, end = before.points.size(); i < end; ++i) {
        if (!removed[i]) {
            after.points.push_back(before.points[i]);
        }
    }
    return make_operation(curve, std::move(after), fmt::format("remove {} point{}", count, (count == 1) ? "" : "s"));
}

auto make_curve_set_points_kind_operation(
    const std::shared_ptr<erhe::scene::Curve>& curve,
    const std::span<const std::size_t>         indices,
    const Curve_point_kind                     kind
) -> std::shared_ptr<Curve_set_operation>
{
    if (!edit_refusal(*curve).empty()) {
        return {};
    }
    Curve_path after = curve->get_path();
    for (const std::size_t index : indices) {
        erhe::math::set_curve_point_kind(after.points, index, after.closure, kind);
    }
    if (paths_equal(after, curve->get_path())) {
        return {};
    }
    return make_operation(curve, std::move(after), fmt::format("points kind = {}", erhe::scene::c_str(kind)));
}

auto make_curve_toggle_closed_operation(const std::shared_ptr<erhe::scene::Curve>& curve) -> std::shared_ptr<Curve_set_operation>
{
    if (!edit_refusal(*curve).empty()) {
        return {};
    }
    Curve_path after = curve->get_path();
    const bool closing = (after.closure == erhe::math::Curve_closure::open);
    after.closure = closing ? erhe::math::Curve_closure::closed : erhe::math::Curve_closure::open;
    return make_operation(curve, std::move(after), closing ? "close" : "open");
}

Curve_tool_command::Curve_tool_command(erhe::commands::Commands& commands, const char* name, Curve_tool& tool, const Curve_tool_action action)
    : Command {commands, name}
    , m_tool  {tool}
    , m_action{action}
{
}

void Curve_tool_command::try_ready()
{
    if (m_tool.can_run(m_action)) {
        set_ready();
    }
}

auto Curve_tool_command::try_call() -> bool
{
    return m_tool.can_run(m_action) && m_tool.run(m_action);
}

Curve_tool::Curve_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools
)
    : Point_set_tool{commands, context, app_message_bus, tools, "Curve.select", "Curve.cancel_edit"}
{
    set_description("Curve Edit");

    const std::pair<const char*, Curve_tool_action> actions[] = {
        {"Curve.remove_points",      Curve_tool_action::remove_points},
        {"Curve.add_point_at_start", Curve_tool_action::add_point_at_start},
        {"Curve.add_point_at_end",   Curve_tool_action::add_point_at_end},
        {"Curve.set_kind_corner",    Curve_tool_action::set_kind_corner},
        {"Curve.set_kind_smooth",    Curve_tool_action::set_kind_smooth},
        {"Curve.set_kind_free",      Curve_tool_action::set_kind_free},
        {"Curve.toggle_closed",      Curve_tool_action::toggle_closed}
    };
    for (const auto& [name, action] : actions) {
        m_commands.push_back(std::make_unique<Curve_tool_command>(commands, name, *this, action));
        m_commands.back()->set_host(this);
        commands.register_command(m_commands.back().get());
    }
    // The exact mask 0 dispatches before the mask-less Selection.delete; it declines without selected points.
    commands.bind_command_to_key(m_commands.front().get(), erhe::window::Key_delete, erhe::commands::Button_trigger::Button_pressed, 0u);

    m_selection_subscription = app_message_bus.selection.subscribe(
        [this](Selection_message&) {
            m_target_stale = true;
            static_cast<void>(update_point_set());
        }
    );
    m_curve_changed_subscription = app_message_bus.curve_changed.subscribe(
        [this](Curve_changed_message& message) {
            if (message.shape && (message.curve == m_target.lock())) {
                prune_selection();
            }
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
            if (!curve) {
                return;
            }
            const std::shared_ptr<Scene_root> scene_root = get_hosting_scene_root(curve.get());
            if (!scene_root || (scene_root == message.scene_root)) {
                set_target({});
            }
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
            if (curve && message.removed->lookup.contains(curve.get())) {
                set_target({});
            }
        }
    );
    m_kit_layout_changed_subscription = app_message_bus.kit_layout_changed.subscribe(
        [this](Kit_layout_changed_message& message) {
            const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
            // A drag of the tool's own changes no kind; a value write during it (MCP) is taken now, the edit keeps its own snapshot.
            if (curve && (static_cast<uint64_t>(curve->get_id()) == message.node_id)) {
                update_segment_handles();
            }
        }
    );
}

auto Curve_tool::get_target_curve() const -> std::shared_ptr<erhe::scene::Curve>
{
    return m_target.lock();
}

void Curve_tool::set_target(const std::shared_ptr<erhe::scene::Curve>& curve)
{
    if (curve == m_target.lock()) {
        return;
    }
    on_cancel_edit();
    m_target = curve;
    m_selection.clear();
    m_selected_segment.reset();
    update_segment_handles();
}

void Curve_tool::update_segment_handles()
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    m_segment_handles = curve && is_segments_layout(*curve);
    if (m_selected_segment.has_value() && (m_selected_segment.value() >= get_segment_count())) {
        m_selected_segment.reset();
    }
}

auto Curve_tool::has_segment_handles() const -> bool
{
    return m_segment_handles && has_point_set_target();
}

auto Curve_tool::get_segment_count() const -> std::size_t
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    return (curve && m_segment_handles) ? erhe::math::get_span_count(curve->get_path()) : 0;
}

auto Curve_tool::is_editing(const erhe::scene::Curve& curve) const -> bool
{
    return m_edit.active && (m_edit.curve.get() == &curve);
}

auto Curve_tool::get_selected_segment() const -> std::optional<std::size_t>
{
    return has_point_set_target() ? m_selected_segment : std::nullopt;
}

auto Curve_tool::get_segment_point_index(const std::size_t segment) const -> std::size_t
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    const std::size_t point_count = curve ? curve->get_path().points.size() : 0;
    return (point_count * parts_per_point) + segment;
}

auto Curve_tool::get_segment_of(const std::size_t index) const -> std::optional<std::size_t>
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve || !m_segment_handles) {
        return std::nullopt;
    }
    const std::size_t first = curve->get_path().points.size() * parts_per_point;
    if ((index < first) || ((index - first) >= erhe::math::get_span_count(curve->get_path()))) {
        return std::nullopt;
    }
    return index - first;
}

auto Curve_tool::select_curve_segment(const std::shared_ptr<erhe::scene::Curve>& curve, const std::optional<std::size_t> segment) -> bool
{
    if (!curve || (m_context.selection == nullptr)) {
        return false;
    }
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected = m_context.selection->get_selected_items();
    if ((selected.size() != 1) || (selected.front() != curve)) {
        m_context.selection->set_selection(std::vector<std::shared_ptr<erhe::Item_base>>{curve});
    }
    m_target_stale = true;
    static_cast<void>(update_point_set());
    if (m_target.lock() != curve) {
        return false;
    }
    if (!segment.has_value()) {
        m_selected_segment.reset();
        return true;
    }
    if (segment.value() >= get_segment_count()) {
        return false;
    }
    m_selection.clear();
    m_selected_segment = segment;
    return true;
}

auto Curve_tool::update_point_set() -> bool
{
    if (m_target_stale && !is_own_edit_active()) {
        m_target_stale = false;
        std::shared_ptr<erhe::scene::Curve> curve;
        if (m_context.selection != nullptr) {
            const std::vector<std::shared_ptr<erhe::Item_base>>& selected = m_context.selection->get_selected_items();
            if (selected.size() == 1) {
                curve = std::dynamic_pointer_cast<erhe::scene::Curve>(selected.front());
            }
        }
        if (curve && !get_hosting_scene_root(curve.get())) {
            curve.reset();
        }
        set_target(curve);
    }
    return has_point_set_target();
}

auto Curve_tool::has_point_set_target() const -> bool
{
    return !m_target.expired();
}

auto Curve_tool::get_point_set_scene_root() const -> std::shared_ptr<Scene_root>
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    return curve ? get_hosting_scene_root(curve.get()) : std::shared_ptr<Scene_root>{};
}

auto Curve_tool::is_valid_index(const Curve_path& path, const std::size_t index) const -> bool
{
    const std::size_t point = index / parts_per_point;
    if (point >= path.points.size()) {
        return false;
    }
    return (part_of(index) == Curve_point_part::position) || (path.points[point].kind != Curve_point_kind::corner);
}

void Curve_tool::get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const
{
    out_positions.clear();
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return;
    }
    const glm::mat4 world_from_node = curve->world_from_node();
    const Curve_path& path = curve->get_path();
    for (const Curve_point& point : path.points) {
        out_positions.push_back(glm::vec3{world_from_node * glm::vec4{point.position, 1.0f}});
        if (point.kind == Curve_point_kind::corner) {
            out_positions.push_back(std::nullopt);
            out_positions.push_back(std::nullopt);
        } else {
            out_positions.push_back(glm::vec3{world_from_node * glm::vec4{point.position + point.in_handle, 1.0f}});
            out_positions.push_back(glm::vec3{world_from_node * glm::vec4{point.position + point.out_handle, 1.0f}});
        }
    }
    // The segment handles at the chord midpoints.
    const std::size_t segment_count = get_segment_count();
    for (std::size_t segment = 0; segment < segment_count; ++segment) {
        const glm::vec3 midpoint = 0.5f * (path.points[segment].position + path.points[(segment + 1) % path.points.size()].position);
        out_positions.push_back(glm::vec3{world_from_node * glm::vec4{midpoint, 1.0f}});
    }
}

auto Curve_tool::get_point_world_position(const std::size_t index, glm::vec3& out_position) const -> bool
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return false;
    }
    const std::optional<std::size_t> segment = get_segment_of(index);
    if (segment.has_value()) {
        const Curve_path& path = curve->get_path();
        const glm::vec3 midpoint = 0.5f * (path.points[segment.value()].position + path.points[(segment.value() + 1) % path.points.size()].position);
        out_position = glm::vec3{curve->world_from_node() * glm::vec4{midpoint, 1.0f}};
        return true;
    }
    if (!is_valid_index(curve->get_path(), index)) {
        return false;
    }
    const Curve_point& point = curve->get_path().points[index / parts_per_point];
    const Curve_point_part part = part_of(index);
    const glm::vec3 local =
        (part == Curve_point_part::position)  ? point.position :
        (part == Curve_point_part::in_handle) ? (point.position + point.in_handle) :
                                                (point.position + point.out_handle);
    out_position = glm::vec3{curve->world_from_node() * glm::vec4{local, 1.0f}};
    return true;
}

auto Curve_tool::is_point_selected(const std::size_t index) const -> bool
{
    if (m_selected_segment.has_value()) {
        const std::optional<std::size_t> segment = get_segment_of(index);
        return segment.has_value() && (segment.value() == m_selected_segment.value());
    }
    return std::binary_search(m_selection.begin(), m_selection.end(), index);
}

auto Curve_tool::has_point_selection() const -> bool
{
    return has_point_set_target() && (!m_selection.empty() || m_selected_segment.has_value());
}

void Curve_tool::select_point(const std::size_t index, const Point_select select)
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return;
    }
    // One segment at a time and never with points: each drags the other ways.
    const std::optional<std::size_t> segment = get_segment_of(index);
    if (segment.has_value()) {
        m_selection.clear();
        const bool deselect = (select == Point_select::toggle) && (m_selected_segment == segment);
        m_selected_segment = deselect ? std::nullopt : segment;
        return;
    }
    if (!is_valid_index(curve->get_path(), index)) {
        return;
    }
    m_selected_segment.reset();
    if (select == Point_select::replace) {
        m_selection.clear();
        m_selection.push_back(index);
        return;
    }
    const std::vector<std::size_t>::iterator i = std::lower_bound(m_selection.begin(), m_selection.end(), index);
    if ((i != m_selection.end()) && (*i == index)) {
        m_selection.erase(i);
    } else {
        m_selection.insert(i, index);
    }
}

auto Curve_tool::clear_point_selection() -> bool
{
    if (m_selection.empty() && !m_selected_segment.has_value()) {
        return false;
    }
    m_selection.clear();
    m_selected_segment.reset();
    return true;
}

void Curve_tool::prune_selection()
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        m_selection.clear();
        return;
    }
    const Curve_path& path = curve->get_path();
    std::erase_if(m_selection, [this, &path](const std::size_t index) { return !is_valid_index(path, index); });
    if (m_selected_segment.has_value() && (m_selected_segment.value() >= get_segment_count())) {
        m_selected_segment.reset();
    }
}

void Curve_tool::get_selected_points(std::vector<Curve_point_ref>& out_refs) const
{
    out_refs.clear();
    for (const std::size_t index : m_selection) {
        out_refs.push_back(Curve_point_ref{.point = index / parts_per_point, .part = part_of(index)});
    }
}

void Curve_tool::get_selected_point_indices(std::vector<std::size_t>& out_indices) const
{
    out_indices.clear();
    for (const std::size_t index : m_selection) {
        const std::size_t point = index / parts_per_point;
        if (out_indices.empty() || (out_indices.back() != point)) {
            out_indices.push_back(point);
        }
    }
}

auto Curve_tool::select_curve_points(const std::shared_ptr<erhe::scene::Curve>& curve, const std::span<const Curve_point_ref> refs, const Point_select select) -> bool
{
    if (!curve || (m_context.selection == nullptr)) {
        return false;
    }
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected = m_context.selection->get_selected_items();
    if ((selected.size() != 1) || (selected.front() != curve)) {
        m_context.selection->set_selection(std::vector<std::shared_ptr<erhe::Item_base>>{curve});
    }
    m_target_stale = true;
    static_cast<void>(update_point_set());
    if (m_target.lock() != curve) {
        return false;
    }
    if ((select == Point_select::replace) || !refs.empty()) {
        m_selected_segment.reset();
    }
    if (select == Point_select::replace) {
        m_selection.clear();
    }
    for (const Curve_point_ref& ref : refs) {
        const std::size_t index = index_of(ref.point, ref.part);
        if (!is_valid_index(curve->get_path(), index)) {
            return false;
        }
        if (!is_point_selected(index)) {
            m_selection.insert(std::lower_bound(m_selection.begin(), m_selection.end(), index), index);
        }
    }
    return true;
}

auto Curve_tool::get_point_frame_rotation() const -> glm::quat
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    }
    const glm::quat node_rotation = curve->world_from_node_transform().get_rotation();
    // A segment handle's Local frame: x along the segment's normal, the one axis its drag moves along.
    glm::vec3 normal{0.0f};
    if (m_selected_segment.has_value() && get_segment_normal(curve->get_path(), m_selected_segment.value(), normal).empty()) {
        return node_rotation * segment_frame_rotation(normal);
    }
    return node_rotation;
}

auto Curve_tool::get_point_drawing() const -> Point_drawing
{
    // Every point and handle end is a handle to grab; curves_visualization() draws the curve and the handle lines.
    return Point_drawing::all;
}

auto Curve_tool::begin_point_edit() -> bool
{
    m_edit = Edit{};
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve || (m_selection.empty() && !m_selected_segment.has_value())) {
        return false;
    }
    const std::string refusal = edit_refusal(*curve);
    if (!refusal.empty()) {
        log_tools->warn("Curve point edit refused: {}", refusal);
        return false;
    }
    m_edit.curve           = curve;
    m_edit.before          = curve->get_path();
    m_edit.world_from_node = curve->world_from_node();
    m_edit.node_from_world = curve->node_from_world();
    if (m_selected_segment.has_value()) {
        const std::size_t segment = m_selected_segment.value();
        const std::string problem = get_segment_normal(m_edit.before, segment, m_edit.segment_normal);
        if (!problem.empty()) {
            log_tools->warn("Curve segment move refused: {}", problem);
            m_edit = Edit{};
            return false;
        }
        m_edit.segment          = segment;
        m_edit.segment_midpoint = 0.5f * (m_edit.before.points[segment].position + m_edit.before.points[(segment + 1) % m_edit.before.points.size()].position);
    }
    m_edit.active          = true;
    if (m_context.level_kits != nullptr) {
        m_context.level_kits->begin_layout_preview(curve);
    }
    return true;
}

void Curve_tool::apply_point_edit(const std::optional<glm::mat4>& world_delta)
{
    if (!m_edit.active) {
        return;
    }
    if (!world_delta.has_value()) {
        if (!paths_equal(m_edit.curve->get_path(), m_edit.before)) {
            m_edit.curve->set_path(m_edit.before);
        }
        return;
    }
    // The delta in the prim's frame; handles are offsets, so they take its linear part.
    const glm::mat4 delta  = m_edit.node_from_world * world_delta.value() * m_edit.world_from_node;
    if (m_edit.segment.has_value()) {
        // Only the midpoint's motion along the normal moves the segment; the end points slide along the neighbours.
        const glm::vec3 moved  = glm::vec3{delta * glm::vec4{m_edit.segment_midpoint, 1.0f}};
        const float     offset = glm::dot(moved - m_edit.segment_midpoint, m_edit.segment_normal);
        Segment_move move;
        if (!move_curve_segment(m_edit.before, m_edit.segment.value(), offset, move).empty()) {
            return;
        }
        m_scratch_path = m_edit.before;
        apply_segment_move(move, m_scratch_path);
        if (!paths_equal(m_scratch_path, m_edit.curve->get_path())) {
            m_edit.curve->set_path(m_scratch_path);
        }
        return;
    }
    const glm::mat3 linear{delta};
    // Alt frees the dragged handle of a smooth point (the point becomes free), read live as the animation window does.
    const bool free_side = (m_context.input_state != nullptr) && m_context.input_state->alt;

    m_scratch_path = m_edit.before;
    for (const std::size_t index : m_selection) {
        const std::size_t point_index = index / parts_per_point;
        if (point_index >= m_scratch_path.points.size()) {
            continue;
        }
        const Curve_point& before = m_edit.before.points[point_index];
        Curve_point&       after  = m_scratch_path.points[point_index];
        const Curve_point_part part = part_of(index);
        if (part == Curve_point_part::position) {
            after.position   = glm::vec3{delta * glm::vec4{before.position, 1.0f}};
            after.in_handle  = linear * before.in_handle;
            after.out_handle = linear * before.out_handle;
            continue;
        }
        if (is_point_selected(index_of(point_index, Curve_point_part::position)) || (before.kind == Curve_point_kind::corner)) {
            continue; // carried by its moved point
        }
        const Curve_handle handle = (part == Curve_point_part::in_handle) ? Curve_handle::in : Curve_handle::out;
        const glm::vec3    offset = (handle == Curve_handle::in) ? before.in_handle : before.out_handle;
        const glm::vec3    end    = glm::vec3{delta * glm::vec4{before.position + offset, 1.0f}};
        if (free_side && (after.kind == Curve_point_kind::smooth)) {
            after.kind = Curve_point_kind::free;
        }
        erhe::math::set_curve_handle(after, handle, end - before.position);
    }
    // A held drag updates every frame; only a change of the path is a change for the curve's users.
    if (!paths_equal(m_scratch_path, m_edit.curve->get_path())) {
        m_edit.curve->set_path(m_scratch_path);
    }
}

void Curve_tool::commit_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    // A path layout gets its children from before the drag back; the operation's follow-ups lay it out once.
    if (m_context.level_kits != nullptr) {
        m_context.level_kits->end_layout_preview();
    }
    const std::shared_ptr<erhe::scene::Curve> curve = std::move(m_edit.curve);
    if (!curve || (m_context.operation_stack == nullptr) || paths_equal(curve->get_path(), m_edit.before)) {
        return;
    }
    std::string what = m_edit.segment.has_value() ? fmt::format("move segment {}", m_edit.segment.value()) : std::string{"move points"};
    m_context.operation_stack->execute_now(
        std::make_shared<Curve_set_operation>(curve, std::move(m_edit.before), curve->get_path(), std::move(what))
    );
}

void Curve_tool::cancel_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    if (m_context.level_kits != nullptr) {
        m_context.level_kits->end_layout_preview();
    }
    const std::shared_ptr<erhe::scene::Curve> curve = std::move(m_edit.curve);
    if (curve && !paths_equal(curve->get_path(), m_edit.before)) {
        curve->set_path(m_edit.before);
    }
}

void Curve_tool::render_extras(const Render_context& context, const std::optional<std::size_t> hovered)
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve || !m_segment_handles) {
        return;
    }
    const std::optional<std::size_t> hovered_segment = hovered.has_value() ? get_segment_of(hovered.value()) : std::nullopt;
    if (!hovered_segment.has_value() && !m_selected_segment.has_value()) {
        return;
    }
    // The hovered and the selected segment's chord, thicker, in the point handles' colors; x-ray as the curves are.
    erhe::renderer::Primitive_renderer line_renderer = context.get(
        {
            .primitive_type    = erhe::graphics::Primitive_type::line,
            .stencil_reference = 2,
            .draw_visible      = true,
            .draw_hidden       = true,
            .thin_lines        = false,
            .xray              = true
        }
    );
    const Debug_visualizations_style& style = context.app_context.editor_settings->debug_visualizations_style;
    const float      width           = 2.0f * style.curve_selected_width;
    const Curve_path& path           = curve->get_path();
    const glm::mat4  world_from_node = curve->world_from_node();
    const auto draw_chord = [&](const std::size_t segment, const glm::vec4& color) {
        if (segment >= erhe::math::get_span_count(path)) {
            return;
        }
        const glm::vec3 a{world_from_node * glm::vec4{path.points[segment].position, 1.0f}};
        const glm::vec3 b{world_from_node * glm::vec4{path.points[(segment + 1) % path.points.size()].position, 1.0f}};
        line_renderer.add_line(color, width, a, color, width, b);
    };
    if (hovered_segment.has_value() && (hovered_segment != m_selected_segment)) {
        draw_chord(hovered_segment.value(), style.point_hover_color);
    }
    if (m_selected_segment.has_value()) {
        draw_chord(m_selected_segment.value(), style.point_selected_color);
    }
}

auto Curve_tool::pick_insert_location(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) const -> std::optional<erhe::math::Curve_location>
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return std::nullopt;
    }
    const erhe::math::Curve_path_table& table = curve->get_table();
    const glm::mat4 world_from_node = curve->world_from_node();
    std::optional<erhe::math::Curve_location> best;
    float best_distance = c_pick_radius;
    for (std::size_t s = 0, end = table.get_span_count(); s < end; ++s) {
        const erhe::math::Curve_span& span = table.get_span(s);
        const int steps = (span.shape == erhe::math::Curve_span_shape::line) ? 1 : static_cast<int>(erhe::scene::Curve::draw_points_per_cubic_span);
        std::optional<glm::vec2> previous;
        float previous_t = 0.0f;
        for (int k = 0; k <= steps; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(steps);
            const glm::vec3 world = glm::vec3{world_from_node * glm::vec4{erhe::math::get_span_position(span, t), 1.0f}};
            const std::optional<glm::vec3> projected = viewport_scene_view.project_to_viewport(world);
            const std::optional<glm::vec2> current = projected.has_value() ? std::optional<glm::vec2>{glm::vec2{projected.value()}} : std::nullopt;
            if (previous.has_value() && current.has_value()) {
                const glm::vec2 a = previous.value();
                const glm::vec2 d = current.value() - a;
                const float length_squared = glm::dot(d, d);
                const float u = (length_squared > 0.0f) ? std::clamp(glm::dot(pointer - a, d) / length_squared, 0.0f, 1.0f) : 0.0f;
                const float distance = glm::distance(a + (u * d), pointer);
                if (distance < best_distance) {
                    best_distance = distance;
                    best = erhe::math::Curve_location{.span = s, .t = previous_t + (u * (t - previous_t))};
                }
            }
            previous   = current;
            previous_t = t;
        }
    }
    return best;
}

auto Curve_tool::try_ready_miss(Viewport_scene_view& viewport_scene_view, const glm::vec2& pointer) -> bool
{
    m_miss_action = Miss_action::none;
    m_insert_location.reset();
    const Input_state* const input_state = m_context.input_state;
    const bool control = (input_state != nullptr) && input_state->control;
    const bool shift   = (input_state != nullptr) && input_state->shift;
    const bool alt     = (input_state != nullptr) && input_state->alt;
    // Ctrl+click on the curve inserts a point there, as Ctrl+click near a curve inserts a key in the animation window.
    if (control && !shift && !alt) {
        m_insert_location = pick_insert_location(viewport_scene_view, pointer);
        if (m_insert_location.has_value()) {
            m_miss_action = Miss_action::insert;
            return true;
        }
        return false;
    }
    // A plain click off the points clears them first; the next one reaches the object selection.
    if (!control && !shift && !alt && has_point_selection()) {
        m_miss_action = Miss_action::clear;
        return true;
    }
    return false;
}

auto Curve_tool::on_select_miss() -> bool
{
    const Miss_action action = m_miss_action;
    m_miss_action = Miss_action::none;
    if (action == Miss_action::clear) {
        return clear_point_selection();
    }
    if ((action != Miss_action::insert) || !m_insert_location.has_value()) {
        return false;
    }
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return false;
    }
    std::size_t index = 0;
    std::string error;
    const std::shared_ptr<Curve_set_operation> operation = make_curve_insert_point_operation(curve, m_insert_location.value(), index, error);
    m_insert_location.reset();
    if (!operation) {
        log_tools->warn("Curve point insert refused: {}", error);
        return true;
    }
    execute(operation);
    m_selection.clear();
    m_selection.push_back(index_of(index, Curve_point_part::position));
    return true;
}

void Curve_tool::execute(const std::shared_ptr<Curve_set_operation>& operation)
{
    if (operation && (m_context.operation_stack != nullptr)) {
        m_context.operation_stack->execute_now(operation);
    }
}

auto Curve_tool::can_run(const Curve_tool_action action) const -> bool
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve || is_own_edit_active()) {
        return false;
    }
    switch (action) {
        case Curve_tool_action::remove_points: {
            return std::any_of(m_selection.begin(), m_selection.end(), [](const std::size_t index) { return part_of(index) == Curve_point_part::position; });
        }
        case Curve_tool_action::add_point_at_start:
        case Curve_tool_action::add_point_at_end: {
            return curve->get_path().closure == erhe::math::Curve_closure::open;
        }
        case Curve_tool_action::set_kind_corner:
        case Curve_tool_action::set_kind_smooth:
        case Curve_tool_action::set_kind_free: {
            return !m_selection.empty();
        }
        case Curve_tool_action::toggle_closed: {
            return true;
        }
        default: {
            return false;
        }
    }
}

auto Curve_tool::run(const Curve_tool_action action) -> bool
{
    const std::shared_ptr<erhe::scene::Curve> curve = m_target.lock();
    if (!curve) {
        return false;
    }
    std::string error;
    switch (action) {
        case Curve_tool_action::remove_points: {
            std::vector<std::size_t> indices;
            for (const std::size_t index : m_selection) {
                if (part_of(index) == Curve_point_part::position) {
                    indices.push_back(index / parts_per_point);
                }
            }
            const std::shared_ptr<Curve_set_operation> operation = make_curve_remove_points_operation(curve, indices, error);
            if (!operation) {
                log_tools->warn("Curve point removal refused: {}", error);
                return true;
            }
            m_selection.clear();
            execute(operation);
            return true;
        }
        case Curve_tool_action::add_point_at_start:
        case Curve_tool_action::add_point_at_end: {
            const erhe::math::Curve_end end = (action == Curve_tool_action::add_point_at_start) ? erhe::math::Curve_end::start : erhe::math::Curve_end::end;
            std::size_t index = 0;
            const std::shared_ptr<Curve_set_operation> operation = make_curve_add_point_operation(curve, end, std::nullopt, index, error);
            if (!operation) {
                log_tools->warn("Curve point add refused: {}", error);
                return true;
            }
            execute(operation);
            m_selection.clear();
            m_selection.push_back(index_of(index, Curve_point_part::position));
            return true;
        }
        case Curve_tool_action::set_kind_corner:
        case Curve_tool_action::set_kind_smooth:
        case Curve_tool_action::set_kind_free: {
            const Curve_point_kind kind =
                (action == Curve_tool_action::set_kind_corner) ? Curve_point_kind::corner :
                (action == Curve_tool_action::set_kind_smooth) ? Curve_point_kind::smooth :
                                                                 Curve_point_kind::free;
            std::vector<std::size_t> indices;
            get_selected_point_indices(indices);
            execute(make_curve_set_points_kind_operation(curve, indices, kind));
            prune_selection();
            return true;
        }
        case Curve_tool_action::toggle_closed: {
            execute(make_curve_toggle_closed_operation(curve));
            return true;
        }
        default: {
            return false;
        }
    }
}

}
