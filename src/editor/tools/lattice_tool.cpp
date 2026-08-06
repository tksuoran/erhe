#include "tools/lattice_tool.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "geometry_graph/geometry_graph.hpp"
#include "geometry_graph/geometry_graph_mesh.hpp"
#include "geometry_graph/graph_mesh.hpp"
#include "geometry_graph/nodes/lattice_node.hpp"
#include "renderers/render_context.hpp"
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

namespace editor {

namespace {

constexpr float     c_pick_radius_px  {12.0f};
constexpr glm::vec4 c_cage_color      {1.0f, 0.6f, 0.1f, 1.0f};
constexpr glm::vec4 c_point_color     {1.0f, 0.6f, 0.1f, 1.0f};
constexpr glm::vec4 c_hover_color     {1.0f, 1.0f, 1.0f, 1.0f};
constexpr glm::vec4 c_selected_color  {1.0f, 1.0f, 0.0f, 1.0f};

[[nodiscard]] auto control_point_local_position(
    const Lattice_node& lattice,
    const glm::vec3&    cage_min,
    const glm::vec3&    cage_max,
    const glm::ivec3    point
) -> glm::vec3
{
    const glm::ivec3 divisions = lattice.get_divisions();
    const glm::vec3  rest =
        cage_min +
        (cage_max - cage_min) * glm::vec3{
            static_cast<float>(point.x) / static_cast<float>(divisions.x),
            static_cast<float>(point.y) / static_cast<float>(divisions.y),
            static_cast<float>(point.z) / static_cast<float>(divisions.z)
        };
    // The transform-driver node parents every control point (identity when unset)
    return glm::vec3{lattice.get_control_point_transform() * glm::vec4{rest + lattice.get_control_point_offset(point), 1.0f}};
}

} // anonymous namespace

Lattice_select_command::Lattice_select_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Lattice.select"}
    , m_context{context}
{
}

void Lattice_select_command::try_ready()
{
    if (m_context.lattice_tool == nullptr) {
        return;
    }
    if (m_context.lattice_tool->try_ready()) {
        set_ready();
    }
}

auto Lattice_select_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    if (m_context.lattice_tool == nullptr) {
        set_inactive();
        return false;
    }
    const bool consumed = m_context.lattice_tool->on_select();
    set_inactive();
    return consumed;
}

Lattice_tool::Lattice_tool(
    erhe::commands::Commands& commands,
    App_context&              context,
    App_message_bus&          app_message_bus,
    Tools&                    tools
)
    : Tool            {context, tools, Tool_flags::background}
    , m_select_command{commands, context}
{
    set_base_priority(c_priority);
    set_description  ("Lattice Edit");

    m_select_command.set_host(this);
    commands.register_command            (&m_select_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::window::Mouse_button_left, false);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );
}

auto Lattice_tool::update_active_lattice() -> const Active_lattice&
{
    // Keep the target stable while a gizmo edit gesture is writing into it.
    Transform_tool* transform_tool = m_context.transform_tool;
    if ((transform_tool != nullptr) && transform_tool->is_component_edit_active()) {
        return m_active;
    }

    m_active = {};
    const std::shared_ptr<Scene_root> scene_root = (m_context.selection != nullptr)
        ? m_context.selection->get_active_scene_root()
        : std::shared_ptr<Scene_root>{};
    if (!scene_root) {
        return m_active;
    }
    scene_root->get_scene().for_each_node([&](const std::shared_ptr<erhe::scene::Node>& node) {
        const std::shared_ptr<Geometry_graph_mesh> attachment = erhe::scene::get_attachment<Geometry_graph_mesh>(node.get());
        if (!attachment) {
            return true;
        }
        const std::shared_ptr<Graph_mesh>& graph_mesh = attachment->get_graph_mesh();
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

auto Lattice_tool::pick_control_point(Scene_view& scene_view) const -> std::optional<glm::ivec3>
{
    if (!m_active.lattice || !m_active.bound_node) {
        return std::nullopt;
    }
    Viewport_scene_view* viewport_scene_view = scene_view.as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return std::nullopt;
    }
    const std::optional<glm::vec2> pointer_opt = viewport_scene_view->get_position_in_viewport();
    if (!pointer_opt.has_value()) {
        return std::nullopt;
    }
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!m_active.lattice->resolve_cage(cage_min, cage_max)) {
        return std::nullopt;
    }
    const glm::vec2  pointer         = pointer_opt.value();
    const glm::mat4  world_from_node = m_active.bound_node->world_from_node();
    const glm::ivec3 divisions       = m_active.lattice->get_divisions();

    std::optional<glm::ivec3> best_point;
    float                     best_depth{0.0f};
    for (int k = 0; k <= divisions.z; ++k) {
        for (int j = 0; j <= divisions.y; ++j) {
            for (int i = 0; i <= divisions.x; ++i) {
                const glm::ivec3 point{i, j, k};
                const glm::vec3  local = control_point_local_position(*m_active.lattice, cage_min, cage_max, point);
                const glm::vec3  world = glm::vec3{world_from_node * glm::vec4{local, 1.0f}};
                const std::optional<glm::vec3> projected = viewport_scene_view->project_to_viewport(world);
                if (!projected.has_value()) {
                    continue;
                }
                const float distance = glm::distance(glm::vec2{projected.value()}, pointer);
                if (distance > c_pick_radius_px) {
                    continue;
                }
                // Nearest in depth among the points under the pointer.
                if (!best_point.has_value() || (projected.value().z > best_depth)) {
                    best_point = point;
                    best_depth = projected.value().z;
                }
            }
        }
    }
    return best_point;
}

auto Lattice_tool::try_ready() -> bool
{
    m_pick_point.reset();
    if (!m_active.lattice || !m_active.bound_node) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    // Only pick in views of the active scene (the scene the target was found in).
    const std::shared_ptr<Scene_root> view_scene_root   = scene_view->get_scene_root();
    const std::shared_ptr<Scene_root> active_scene_root = (m_context.selection != nullptr)
        ? m_context.selection->get_active_scene_root()
        : std::shared_ptr<Scene_root>{};
    if (!view_scene_root || (view_scene_root != active_scene_root)) {
        return false;
    }
    // The gizmo handles win over control point picking.
    Transform_tool* transform_tool = m_context.transform_tool;
    if ((transform_tool != nullptr) && (transform_tool->get_hover_handle() != Handle::e_handle_none)) {
        return false;
    }
    m_pick_point = pick_control_point(*scene_view);
    return m_pick_point.has_value();
}

auto Lattice_tool::on_select() -> bool
{
    if (!m_pick_point.has_value() || !m_active.lattice) {
        return false;
    }
    m_active.lattice->set_selected_point(m_pick_point.value());
    m_pick_point.reset();
    return true;
}

void Lattice_tool::append_point_quad(
    const glm::vec3& position_in_world,
    const glm::vec3& camera_right,
    const glm::vec3& camera_up,
    const float      half_size
)
{
    const glm::vec3 r    = half_size * camera_right;
    const glm::vec3 u    = half_size * camera_up;
    const uint32_t  base = static_cast<uint32_t>(m_scratch_positions.size());
    m_scratch_positions.push_back(position_in_world - r - u);
    m_scratch_positions.push_back(position_in_world + r - u);
    m_scratch_positions.push_back(position_in_world + r + u);
    m_scratch_positions.push_back(position_in_world - r + u);
    m_scratch_indices.push_back(base + 0);
    m_scratch_indices.push_back(base + 1);
    m_scratch_indices.push_back(base + 2);
    m_scratch_indices.push_back(base + 0);
    m_scratch_indices.push_back(base + 2);
    m_scratch_indices.push_back(base + 3);
}

void Lattice_tool::tool_render(const Render_context& context)
{
    // Desktop viewport only: the billboard quads go through Debug_renderer's
    // direct triangle path, which is single-view only (see the equivalent
    // gate in Mesh_component_selection_tool::tool_render).
    if (context.viewport_scene_view == nullptr) {
        return;
    }
    if (!m_active.lattice || !m_active.bound_node) {
        return;
    }
    // Draw only in views of the active scene (where the target lives and
    // where the gizmo operates).
    const std::shared_ptr<Scene_root> view_scene_root   = context.scene_view.get_scene_root();
    const std::shared_ptr<Scene_root> active_scene_root = (m_context.selection != nullptr)
        ? m_context.selection->get_active_scene_root()
        : std::shared_ptr<Scene_root>{};
    if (!view_scene_root || (view_scene_root != active_scene_root)) {
        return;
    }
    glm::vec3 cage_min{0.0f};
    glm::vec3 cage_max{0.0f};
    if (!m_active.lattice->resolve_cage(cage_min, cage_max)) {
        return;
    }
    const glm::mat4  world_from_node = m_active.bound_node->world_from_node();
    const glm::ivec3 divisions       = m_active.lattice->get_divisions();
    const glm::ivec3 selected        = m_active.lattice->get_selected_point();

    const auto local_position = [&](const glm::ivec3 point) -> glm::vec3 {
        return control_point_local_position(*m_active.lattice, cage_min, cage_max, point);
    };

    // Cage wireframe (deformed lattice), in the bound node's local space.
    // stencil_reference must be non-zero (function=greater against a
    // zero-cleared stencil buffer); 2 matches the other debug tools.
    erhe::renderer::Primitive_renderer line_renderer = context.get({erhe::graphics::Primitive_type::line, 2, true, true});
    std::vector<erhe::renderer::Line> lines;
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

    // Billboarded control point handles: normal / hovered / selected.
    glm::vec3 camera_position{0.0f};
    glm::vec3 camera_right   {1.0f, 0.0f, 0.0f};
    glm::vec3 camera_up      {0.0f, 1.0f, 0.0f};
    const erhe::scene::Node* camera_node = context.get_camera_node();
    if (camera_node != nullptr) {
        const glm::mat4 world_from_camera = camera_node->world_from_node();
        camera_position = glm::vec3{world_from_camera[3]};
        camera_right    = glm::normalize(glm::vec3{world_from_camera[0]});
        camera_up       = glm::normalize(glm::vec3{world_from_camera[1]});
    }
    const std::optional<glm::ivec3> hovered = context.viewport_scene_view->is_scene_view_hovered()
        ? pick_control_point(context.scene_view)
        : std::nullopt;

    erhe::renderer::Primitive_renderer triangle_renderer = context.get({erhe::graphics::Primitive_type::triangle, 2, true, false});
    const auto draw_points = [&](const glm::vec4& color, const float size_factor, const auto& predicate) {
        m_scratch_positions.clear();
        m_scratch_indices.clear();
        for (int k = 0; k <= divisions.z; ++k) {
            for (int j = 0; j <= divisions.y; ++j) {
                for (int i = 0; i <= divisions.x; ++i) {
                    const glm::ivec3 point{i, j, k};
                    if (!predicate(point)) {
                        continue;
                    }
                    const glm::vec3 world     = glm::vec3{world_from_node * glm::vec4{local_position(point), 1.0f}};
                    const float     half_size = size_factor * glm::distance(camera_position, world);
                    append_point_quad(world, camera_right, camera_up, half_size);
                }
            }
        }
        if (!m_scratch_indices.empty()) {
            triangle_renderer.add_triangles(glm::mat4{1.0f}, color, m_scratch_positions, m_scratch_indices);
        }
    };
    draw_points(c_point_color, 0.006f, [&](const glm::ivec3 p) {
        return (p != selected) && (!hovered.has_value() || (p != hovered.value()));
    });
    if (hovered.has_value() && (hovered.value() != selected)) {
        draw_points(c_hover_color, 0.008f, [&](const glm::ivec3 p) { return p == hovered.value(); });
    }
    draw_points(c_selected_color, 0.009f, [&](const glm::ivec3 p) { return p == selected; });
}

}
