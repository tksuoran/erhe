#include "tools/lattice_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_operations.hpp"
#include "geometry_graph/geometry_graph_window.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/nodes/lattice_node.hpp"
#include "renderers/render_context.hpp"
#include "operations/operation_stack.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <cmath>

namespace editor {

namespace {

constexpr glm::vec4 c_cage_color{1.0f, 0.6f, 0.1f, 1.0f};

[[nodiscard]] auto control_point_rest_position(const Lattice_node& lattice, const glm::vec3& cage_min, const glm::vec3& cage_max, const glm::ivec3 point) -> glm::vec3
{
    const glm::ivec3 divisions = lattice.get_divisions();
    return
        cage_min +
        (cage_max - cage_min) * glm::vec3{
            static_cast<float>(point.x) / static_cast<float>(divisions.x),
            static_cast<float>(point.y) / static_cast<float>(divisions.y),
            static_cast<float>(point.z) / static_cast<float>(divisions.z)
        };
}

// Inverse of the driver transform, for mapping a gizmo-moved control point
// position back into an offset. Falls back to identity for a degenerate
// (non-invertible) driver transform - the drag then edits pre-transform space.
[[nodiscard]] auto safe_inverse(const glm::mat4& transform) -> glm::mat4
{
    const float determinant = glm::determinant(transform);
    if (std::abs(determinant) < 1e-12f) {
        return glm::mat4{1.0f};
    }
    return glm::inverse(transform);
}

[[nodiscard]] auto control_point_local_position(
    const Lattice_node& lattice,
    const glm::vec3&    cage_min,
    const glm::vec3&    cage_max,
    const glm::ivec3    point
) -> glm::vec3
{
    const glm::vec3 rest = control_point_rest_position(lattice, cage_min, cage_max, point);
    // The transform-driver node parents every control point (identity when unset)
    return glm::vec3{lattice.get_control_point_transform() * glm::vec4{rest + lattice.get_control_point_offset(point), 1.0f}};
}

} // anonymous namespace

Lattice_tool::Lattice_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools
)
    : Point_set_tool{commands, context, app_message_bus, tools, "Lattice.select", "Lattice.cancel_edit"}
{
    set_description("Lattice Edit");

    m_graph_mesh_binding_changed_subscription = app_message_bus.graph_mesh_binding_changed.subscribe(
        [this](Graph_mesh_binding_changed_message&) {
            m_active_stale = true;
        }
    );
    m_close_scene_subscription = app_message_bus.close_scene.subscribe(
        [this](Close_scene_message& message) {
            // Drop the closing scene's node and graph now (doc/editor/coding_rules.md "Scene-hosted references").
            if (message.scene_root == m_active_scene_root.lock()) {
                on_cancel_edit();
            }
            m_active = {};
            m_active_scene_root.reset();
            m_active_stale = true;
        }
    );
    m_items_removed_subscription = app_message_bus.items_removed.subscribe(
        [this](Items_removed_message& message) {
            on_items_removed(*message.removed.get());
        }
    );
}

void Lattice_tool::on_items_removed(const Removed_items& removed)
{
    // Same frame as the removal; the queued binding message arrives a frame later.
    const std::shared_ptr<Graph_mesh> graph_mesh = m_active.lattice ? m_active.lattice->get_owning_graph_mesh() : std::shared_ptr<Graph_mesh>{};
    const bool removed_target =
        (m_active.bound_node && removed.lookup.contains(m_active.bound_node.get())) ||
        (m_active.lattice    && removed.lookup.contains(m_active.lattice.get())) ||
        (graph_mesh          && removed.lookup.contains(graph_mesh.get()));
    if (removed_target) {
        on_cancel_edit();
        m_active = {};
        m_active_stale = true;
    }
}

auto Lattice_tool::update_active_lattice() -> const Active_lattice&
{
    // Keep the target stable while a gizmo edit gesture is writing into it.
    Transform_tool* transform_tool = m_context.transform_tool;
    if ((transform_tool != nullptr) && transform_tool->is_component_edit_active()) {
        return m_active;
    }

    const std::shared_ptr<Scene_root> scene_root = (m_context.selection != nullptr)
        ? m_context.selection->get_active_scene_root()
        : std::shared_ptr<Scene_root>{};
    // The effective active scene (Selection fallbacks: last hovered viewport,
    // single open scene) publishes no change message, so its identity is compared.
    if (scene_root != m_active_scene_root.lock()) {
        m_active_stale = true;
    }
    if (!m_active_stale) {
        return m_active;
    }
    m_active_stale      = false;
    m_active_scene_root = scene_root;
    m_active            = {};
    if (!scene_root) {
        return m_active;
    }
    scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        const std::optional<Geometry_graph_mesh_data> data = read_geometry_graph_mesh(*node.get());
        if (!data.has_value()) {
            return true;
        }
        const std::shared_ptr<Graph_mesh>& graph_mesh = data.value().graph_mesh;
        if (!graph_mesh) {
            return true;
        }
        Geometry_graph& graph = graph_mesh->graph();
        // The designation is the edit-mode switch: display shows the lattice
        // output itself, ghost overlays it as wireframe - either way the user
        // sees the deformation, so either enables viewport editing.
        for (const std::size_t id : { graph.get_display_node_id(), graph.get_ghost_node_id() }) {
            Geometry_graph_node* designated = graph.find_node_by_log_id(id);
            auto* lattice_raw = dynamic_cast<Lattice_node*>(designated);
            if (lattice_raw == nullptr) {
                continue;
            }
            m_active.lattice    = std::dynamic_pointer_cast<Lattice_node>(lattice_raw->node_from_this());
            m_active.bound_node = node;
            return false;
        }
        return true;
    });
    return m_active;
}

auto Lattice_tool::get_active_lattice() const -> const Active_lattice&
{
    return m_active;
}

auto Lattice_tool::point_from_index(const std::size_t index) const -> glm::ivec3
{
    const glm::ivec3 divisions = m_active.lattice->get_divisions();
    const std::size_t nx = static_cast<std::size_t>(divisions.x) + 1;
    const std::size_t ny = static_cast<std::size_t>(divisions.y) + 1;
    return glm::ivec3{
        static_cast<int>(index % nx),
        static_cast<int>((index / nx) % ny),
        static_cast<int>(index / (nx * ny))
    };
}

auto Lattice_tool::update_point_set() -> bool
{
    const Active_lattice& active = update_active_lattice();
    return active.lattice && active.bound_node;
}

auto Lattice_tool::has_point_set_target() const -> bool
{
    return m_active.lattice && m_active.bound_node;
}

auto Lattice_tool::get_point_set_scene_root() const -> std::shared_ptr<Scene_root>
{
    // The active scene: where the target was found and where the gizmo operates.
    if (!has_point_set_target() || (m_context.selection == nullptr)) {
        return {};
    }
    return m_context.selection->get_active_scene_root();
}

auto Lattice_tool::get_point_count() const -> std::size_t
{
    if (!has_point_set_target()) {
        return 0;
    }
    const glm::ivec3 divisions = m_active.lattice->get_divisions();
    return static_cast<std::size_t>(divisions.x + 1) * static_cast<std::size_t>(divisions.y + 1) * static_cast<std::size_t>(divisions.z + 1);
}

void Lattice_tool::get_point_world_positions(std::vector<std::optional<glm::vec3>>& out_positions) const
{
    out_positions.clear();
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!has_point_set_target() || !m_active.lattice->resolve_cage(cage_min, cage_max)) {
        return;
    }
    const glm::mat4 world_from_node = m_active.bound_node->world_from_node();
    for (std::size_t i = 0, end = get_point_count(); i < end; ++i) {
        const glm::vec3 local = control_point_local_position(*m_active.lattice, cage_min, cage_max, point_from_index(i));
        out_positions.push_back(glm::vec3{world_from_node * glm::vec4{local, 1.0f}});
    }
}

auto Lattice_tool::is_point_selected(const std::size_t index) const -> bool
{
    return has_point_set_target() && (index < get_point_count()) && (point_from_index(index) == m_active.lattice->get_selected_point());
}

auto Lattice_tool::has_point_selection() const -> bool
{
    return has_point_set_target();
}

void Lattice_tool::select_point(const std::size_t index, const Point_select)
{
    // One selected point: a toggle selects it too.
    if (has_point_set_target() && (index < get_point_count())) {
        m_active.lattice->set_selected_point(point_from_index(index));
    }
}

auto Lattice_tool::clear_point_selection() -> bool
{
    return false;
}

auto Lattice_tool::get_point_frame_rotation() const -> glm::quat
{
    return has_point_set_target() ? m_active.bound_node->world_from_node_transform().get_rotation() : glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
}

auto Lattice_tool::get_point_drawing() const -> Point_drawing
{
    return Point_drawing::all;
}

auto Lattice_tool::begin_point_edit() -> bool
{
    m_edit = Edit{};
    if (!has_point_set_target()) {
        return false;
    }
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!m_active.lattice->resolve_cage(cage_min, cage_max)) {
        return false;
    }
    m_edit.lattice           = m_active.lattice;
    m_edit.point             = m_active.lattice->get_selected_point();
    m_edit.rest_local        = control_point_rest_position(*m_active.lattice, cage_min, cage_max, m_edit.point);
    m_edit.before_offset     = m_active.lattice->get_control_point_offset(m_edit.point);
    m_edit.world_from_node   = m_active.bound_node->world_from_node();
    m_edit.node_from_world   = m_active.bound_node->node_from_world();
    m_edit.driver_transform  = m_active.lattice->get_control_point_transform();
    m_edit.driver_inverse    = safe_inverse(m_edit.driver_transform);
    m_edit.before_parameters = m_active.lattice->dump_parameters();
    m_edit.active            = true;
    return true;
}

void Lattice_tool::apply_point_edit(const std::optional<glm::mat4>& world_delta)
{
    if (!m_edit.active) {
        return;
    }
    const std::shared_ptr<Lattice_node> lattice = m_edit.lattice.lock();
    if (!lattice) {
        return;
    }
    if (!world_delta.has_value()) {
        lattice->set_control_point_offset(m_edit.point, m_edit.before_offset);
        return;
    }
    // Forward: offset -> driver transform -> bound node -> world; the gizmo
    // delta applies in world, then the chain inverts back to an offset.
    const glm::vec3 before_local = glm::vec3{m_edit.driver_transform * glm::vec4{m_edit.rest_local + m_edit.before_offset, 1.0f}};
    const glm::vec3 before_world = glm::vec3{m_edit.world_from_node * glm::vec4{before_local, 1.0f}};
    const glm::vec3 after_world  = glm::vec3{world_delta.value() * glm::vec4{before_world, 1.0f}};
    const glm::vec3 after_local  = glm::vec3{m_edit.node_from_world * glm::vec4{after_world, 1.0f}};
    const glm::vec3 after_pre    = glm::vec3{m_edit.driver_inverse * glm::vec4{after_local, 1.0f}};
    lattice->set_control_point_offset(m_edit.point, after_pre - m_edit.rest_local);
}

void Lattice_tool::commit_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    const std::shared_ptr<Lattice_node> lattice = m_edit.lattice.lock();
    if (!lattice || (m_context.geometry_graph_window == nullptr) || (m_context.operation_stack == nullptr)) {
        return;
    }
    std::string after_parameters = lattice->dump_parameters();
    if (after_parameters == m_edit.before_parameters) {
        return;
    }
    const std::shared_ptr<Geometry_graph_node> node = std::dynamic_pointer_cast<Geometry_graph_node>(lattice->node_from_this());
    m_context.operation_stack->execute_now(
        std::make_shared<Geometry_graph_parameter_operation>(
            *m_context.geometry_graph_window,
            node,
            std::move(m_edit.before_parameters),
            std::move(after_parameters)
        )
    );
}

void Lattice_tool::cancel_point_edit()
{
    if (!m_edit.active) {
        return;
    }
    m_edit.active = false;
    const std::shared_ptr<Lattice_node> lattice = m_edit.lattice.lock();
    if (lattice) {
        lattice->set_control_point_offset(m_edit.point, m_edit.before_offset);
    }
}

void Lattice_tool::render_extras(const Render_context& context, std::optional<std::size_t>)
{
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!m_active.lattice->resolve_cage(cage_min, cage_max)) {
        return;
    }
    const glm::mat4  world_from_node = m_active.bound_node->world_from_node();
    const glm::ivec3 divisions       = m_active.lattice->get_divisions();

    const auto local_position = [&](const glm::ivec3 point) -> glm::vec3 {
        return control_point_local_position(*m_active.lattice, cage_min, cage_max, point);
    };

    // Cage wireframe (deformed lattice), in the bound node's local space.
    // stencil_reference must be non-zero (function=greater against a
    // zero-cleared stencil buffer); 2 matches the other debug tools.
    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    std::vector<erhe::renderer::Line>& lines = m_scratch_lines;
    lines.clear();
    for (int k = 0; k <= divisions.z; ++k) {
        for (int j = 0; j <= divisions.y; ++j) {
            for (int i = 0; i <= divisions.x; ++i) {
                const glm::vec3 p = local_position(glm::ivec3{i, j, k});
                if (i < divisions.x) { lines.push_back({p, local_position(glm::ivec3{i + 1, j, k})}); }
                if (j < divisions.y) { lines.push_back({p, local_position(glm::ivec3{i, j + 1, k})}); }
                if (k < divisions.z) { lines.push_back({p, local_position(glm::ivec3{i, j, k + 1})}); }
            }
        }
    }
    line_renderer.set_thickness(2.0f);
    line_renderer.add_lines(world_from_node, c_cage_color, lines);
}

}
