#include "tools/mesh_component_selection_tool.hpp"

#include "tools/mesh_component_selection.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/viewport_config.hpp"
#include "input_state.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include "erhe_imgui/imgui_helpers.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;
using erhe::geometry::mesh_facet_normalf;

namespace editor {

namespace {

// Per-edge surface frame for the two-face "tent" used by surface-aligned edge
// lines: the edge's two adjacent face normals (world space) plus the
// interior-tangent sign of face A. sign_a is chosen so that
//   sign_a * cross(normal_a, edge_direction_world)
// points toward face A's interior; the compute line shader projects that
// tangent to decide which face governs each screen side of the wide-line
// ribbon. A boundary edge (single facet) sets normal_b = normal_a so the tent
// degenerates to hugging one plane. All-zero normals mean "ordinary line, no
// bias". See Primitive_renderer::add_surface_lines / compute_before_line.comp.
class Edge_surface_frame
{
public:
    glm::vec3 normal_a{0.0f};
    glm::vec3 normal_b{0.0f};
    float     sign_a  {0.0f};
};

[[nodiscard]] auto compute_edge_surface_frame(
    const erhe::geometry::Geometry& geometry,
    const glm::mat4&                world_from_node,
    const glm::mat3&                normal_matrix,
    const GEO::index_t              v0,
    const GEO::index_t              v1
) -> Edge_surface_frame
{
    Edge_surface_frame frame{};
    const GEO::Mesh& geo_mesh = geometry.get_mesh();

    // Find the (up to two) facets that have v0..v1 as one of their edges.
    GEO::index_t facet_a = GEO::NO_INDEX;
    GEO::index_t facet_b = GEO::NO_INDEX;
    for (const GEO::index_t corner : geometry.get_vertex_corners(v0)) {
        const GEO::index_t facet = geometry.get_corner_facet(corner);
        if (facet == GEO::NO_INDEX) {
            continue;
        }
        const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
        const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
        const GEO::index_t corner_count = corner_end - corner_begin;
        if (corner_count < 3) {
            continue;
        }
        bool has_edge = false;
        for (GEO::index_t c = corner_begin; c < corner_end; ++c) {
            if (geo_mesh.facet_corners.vertex(c) != v0) {
                continue;
            }
            const GEO::index_t local      = c - corner_begin;
            const GEO::index_t c_next      = corner_begin + ((local + 1) % corner_count);
            const GEO::index_t c_prev      = corner_begin + ((local + corner_count - 1) % corner_count);
            const GEO::index_t vertex_next = geo_mesh.facet_corners.vertex(c_next);
            const GEO::index_t vertex_prev = geo_mesh.facet_corners.vertex(c_prev);
            if ((vertex_next == v1) || (vertex_prev == v1)) {
                has_edge = true;
            }
            break;
        }
        if (!has_edge) {
            continue;
        }
        if (facet_a == GEO::NO_INDEX) {
            facet_a = facet;
        } else if (facet != facet_a) {
            facet_b = facet;
            break;
        }
    }

    if (facet_a == GEO::NO_INDEX) {
        return frame; // no adjacent facet -> zero normals -> unbiased flat line
    }

    const glm::vec3 p0_local   = to_glm_vec3(get_pointf(geo_mesh.vertices, v0));
    const glm::vec3 p1_local   = to_glm_vec3(get_pointf(geo_mesh.vertices, v1));
    const glm::vec3 world_p0   = glm::vec3{world_from_node * glm::vec4{p0_local, 1.0f}};
    const glm::vec3 world_p1   = glm::vec3{world_from_node * glm::vec4{p1_local, 1.0f}};
    const glm::vec3 edge_world = world_p1 - world_p0;

    const glm::vec3 normal_a_local = to_glm_vec3(mesh_facet_normalf(geo_mesh, facet_a));
    const glm::vec3 normal_a_world = normal_matrix * normal_a_local;
    const float     normal_a_len   = glm::length(normal_a_world);
    frame.normal_a = (normal_a_len > 1e-6f) ? (normal_a_world / normal_a_len) : glm::vec3{0.0f};

    // Interior-tangent sign for face A: sign so that sign * cross(n_a, edge)
    // points from the edge midpoint toward face A's centroid.
    glm::vec3    centroid_a_local{0.0f};
    GEO::index_t centroid_count = 0;
    {
        const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet_a);
        const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet_a);
        for (GEO::index_t c = corner_begin; c < corner_end; ++c) {
            centroid_a_local += to_glm_vec3(get_pointf(geo_mesh.vertices, geo_mesh.facet_corners.vertex(c)));
            ++centroid_count;
        }
    }
    if (centroid_count > 0) {
        centroid_a_local /= static_cast<float>(centroid_count);
    }
    const glm::vec3 centroid_a_world = glm::vec3{world_from_node * glm::vec4{centroid_a_local, 1.0f}};
    const glm::vec3 edge_mid_world   = 0.5f * (world_p0 + world_p1);
    const glm::vec3 to_interior_a    = centroid_a_world - edge_mid_world;
    const glm::vec3 tangent_a        = glm::cross(frame.normal_a, edge_world);
    frame.sign_a = (glm::dot(tangent_a, to_interior_a) >= 0.0f) ? 1.0f : -1.0f;

    // Face B, or fall back to A for a boundary edge (single-plane hug).
    if (facet_b != GEO::NO_INDEX) {
        const glm::vec3 normal_b_local = to_glm_vec3(mesh_facet_normalf(geo_mesh, facet_b));
        const glm::vec3 normal_b_world = normal_matrix * normal_b_local;
        const float     normal_b_len   = glm::length(normal_b_world);
        frame.normal_b = (normal_b_len > 1e-6f) ? (normal_b_world / normal_b_len) : frame.normal_a;
    } else {
        frame.normal_b = frame.normal_a;
    }
    return frame;
}

} // anonymous namespace

#pragma region Commands
Component_select_command::Component_select_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.select"}
    , m_context{context}
{
}

void Component_select_command::try_ready()
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return;
    }
    if (m_context.mesh_component_selection_tool->try_ready()) {
        set_ready();
    }
}

auto Component_select_command::try_call() -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    if (m_context.mesh_component_selection_tool == nullptr) {
        set_inactive();
        return false;
    }
    const bool consumed = m_context.mesh_component_selection_tool->on_select();
    set_inactive();
    return consumed;
}

Component_box_select_command::Component_box_select_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.box_select"}
    , m_context{context}
{
}

void Component_box_select_command::try_ready()
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return;
    }
    if (m_context.mesh_component_selection_tool->box_select_try_ready()) {
        set_ready();
    }
}

auto Component_box_select_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    // accept_mouse_command allows multiple Ready commands on the same button, and
    // a click without motion can leave this drag command armed (Ready). If the
    // gesture sub-mode changed to Click/Paint since then, drop it now instead of
    // box-selecting (and instead of becoming the active mouse command, which
    // would block the paint drag).
    if (!m_context.mesh_component_selection_tool->is_gesture_box_mode()) {
        set_inactive();
        return false;
    }
    // The drag binding has already set the command Active before calling this
    // (on the first real motion). The per-frame held-button tick re-enters with
    // a zero dummy input (absolute (0,0)); treat that as "no new cursor, just
    // re-scan the current box" rather than a real motion to window origin.
    const glm::vec2 absolute_value = input.variant.vector2.absolute_value;
    const bool      real_motion    = (absolute_value.x != 0.0f) || (absolute_value.y != 0.0f);
    m_context.mesh_component_selection_tool->box_select_update(absolute_value, real_motion);
    return true;
}

void Component_box_select_command::on_inactive()
{
    if (m_context.mesh_component_selection_tool != nullptr) {
        m_context.mesh_component_selection_tool->box_select_release();
    }
}

Component_gesture_update_command::Component_gesture_update_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.gesture_update"}
    , m_context{context}
{
}

auto Component_gesture_update_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool != nullptr) {
        m_context.mesh_component_selection_tool->gesture_update();
    }
    return false; // never consumes
}

Component_paint_select_command::Component_paint_select_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.paint_select"}
    , m_context{context}
{
}

void Component_paint_select_command::try_ready()
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return;
    }
    if (m_context.mesh_component_selection_tool->paint_select_try_ready()) {
        set_ready();
    }
}

auto Component_paint_select_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    // Drop a command left armed (Ready) by a prior click after the sub-mode
    // changed away from Paint.
    if (!m_context.mesh_component_selection_tool->is_gesture_paint_mode()) {
        set_inactive();
        return false;
    }
    const glm::vec2 absolute_value = input.variant.vector2.absolute_value;
    const bool      real_motion    = (absolute_value.x != 0.0f) || (absolute_value.y != 0.0f);
    m_context.mesh_component_selection_tool->paint_select_update(absolute_value, real_motion);
    return true;
}

void Component_paint_select_command::on_inactive()
{
    if (m_context.mesh_component_selection_tool != nullptr) {
        m_context.mesh_component_selection_tool->paint_select_release();
    }
}

Component_brush_radius_command::Component_brush_radius_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.brush_radius"}
    , m_context{context}
{
}

auto Component_brush_radius_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    if (!m_context.mesh_component_selection_tool->is_gesture_paint_mode()) {
        return false; // not consumed -> the wheel falls through to fly-camera zoom
    }
    m_context.mesh_component_selection_tool->adjust_brush_radius(input.variant.vector2.relative_value.y);
    return true;
}

Component_gesture_hotkey_command::Component_gesture_hotkey_command(
    erhe::commands::Commands& commands,
    App_context&              context,
    const char*               name,
    const Component_gesture_mode mode
)
    : Command  {commands, name}
    , m_context{context}
    , m_mode   {mode}
{
}

auto Component_gesture_hotkey_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->try_set_gesture_hotkey(m_mode);
}
#pragma endregion Commands

Mesh_component_selection_tool::Mesh_component_selection_tool(
    erhe::commands::Commands&    commands,
    App_context&                 context,
    App_message_bus&             app_message_bus,
    Mesh_component_selection&    mesh_component_selection,
    Tools&                       tools
)
    : Tool                      {context, tools, Tool_flags::background}
    , m_mesh_component_selection{mesh_component_selection}
    , m_select_command          {commands, context}
    , m_box_select_command      {commands, context}
    , m_gesture_update_command  {commands, context}
    , m_paint_select_command    {commands, context}
    , m_brush_radius_command    {commands, context}
    , m_box_hotkey_command      {commands, context, "Mesh_component_selection.hotkey_box",   Component_gesture_mode::box}
    , m_paint_hotkey_command    {commands, context, "Mesh_component_selection.hotkey_paint", Component_gesture_mode::paint}
{
    set_base_priority(c_priority);
    set_description  ("Mesh Component Selection");

    m_select_command.set_host(this);
    commands.register_command            (&m_select_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::window::Mouse_button_left, false);

    // Box-select drag (Box gesture sub-mode). call_on_button_down_without_motion
    // is false so a click without motion never activates the box; the single
    // click command above handles it instead. The update command drives the
    // deferred commit once per frame.
    m_box_select_command.set_host(this);
    commands.register_command          (&m_box_select_command);
    commands.bind_command_to_mouse_drag(&m_box_select_command, erhe::window::Mouse_button_left, false);

    m_gesture_update_command.set_host(this);
    commands.register_command     (&m_gesture_update_command);
    commands.bind_command_to_update(&m_gesture_update_command);

    // Paint-select drag (Paint gesture sub-mode). call_on_button_down_without_motion
    // is true so a click without motion paints one dab (the single-click command
    // is suppressed in paint mode, see try_ready).
    m_paint_select_command.set_host(this);
    commands.register_command          (&m_paint_select_command);
    commands.bind_command_to_mouse_drag(&m_paint_select_command, erhe::window::Mouse_button_left, true);

    // Brush radius wheel. gesture_update keeps it Ready while paint-selecting so
    // it wins the wheel over fly-camera zoom (sort_mouse_wheel_bindings).
    m_brush_radius_command.set_host(this);
    commands.register_command          (&m_brush_radius_command);
    commands.bind_command_to_mouse_wheel(&m_brush_radius_command);

    // B -> Box, C -> Paint (shortcuts for the gesture combo). Gated to Face mode
    // in try_set_gesture_hotkey, so C still falls through to brush preview etc.
    m_box_hotkey_command.set_host(this);
    commands.register_command(&m_box_hotkey_command);
    commands.bind_command_to_key(&m_box_hotkey_command, erhe::window::Key_b, true);

    m_paint_hotkey_command.set_host(this);
    commands.register_command(&m_paint_hotkey_command);
    commands.bind_command_to_key(&m_paint_hotkey_command, erhe::window::Key_c, true);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            Tool::on_message(message);
        }
    );
}

auto Mesh_component_selection_tool::pick(Scene_view& scene_view) const -> Pick_result
{
    Pick_result result;

    const Hover_entry& content = scene_view.get_hover(Hover_entry::content_slot);
    std::shared_ptr<erhe::scene::Mesh> mesh = content.scene_mesh_weak.lock();
    if (
        !content.valid                ||
        !content.position.has_value() ||
        !content.geometry             ||
        !mesh                         ||
        (content.facet == GEO::NO_INDEX) ||
        (content.scene_mesh_primitive_index == std::numeric_limits<std::size_t>::max())
    ) {
        return result;
    }

    // Initial scope: non-skinned meshes only. Skinned meshes deform on the GPU
    // and their CPU geometry positions do not match the rendered pose.
    if (mesh->skin) {
        return result;
    }

    // Edit-locked meshes (e.g. the room floor) are not valid component-selection
    // targets. pick() is the single choke point for try_ready(), on_select() and
    // the hover highlight, so rejecting here suppresses selection and hover both.
    if (mesh->is_lock_edit()) {
        return result;
    }

    const erhe::scene::Node* node = mesh->get_node();
    if (node == nullptr) {
        return result;
    }

    const GEO::Mesh&   geo_mesh     = content.geometry->get_mesh();
    const GEO::index_t facet        = content.facet;
    const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
    const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
    const GEO::index_t corner_count = corner_end - corner_begin;
    if (corner_count < 3) {
        return result;
    }

    result.valid           = true;
    result.mesh            = mesh;
    result.primitive_index = content.scene_mesh_primitive_index;
    result.geometry        = content.geometry;
    result.facet           = facet;

    const glm::vec3 hover_in_mesh = node->transform_point_from_world_to_local(content.position.value());

    // Nearest facet-corner vertex.
    float        nearest_vertex_d2 = std::numeric_limits<float>::max();
    GEO::index_t nearest_vertex    = geo_mesh.facet_corners.vertex(corner_begin);
    for (GEO::index_t corner = corner_begin; corner < corner_end; ++corner) {
        const GEO::index_t vertex = geo_mesh.facet_corners.vertex(corner);
        const glm::vec3    p      = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
        const float        d2     = glm::distance2(hover_in_mesh, p);
        if (d2 < nearest_vertex_d2) {
            nearest_vertex_d2 = d2;
            nearest_vertex    = vertex;
        }
    }
    result.vertex = nearest_vertex;

    // Nearest facet boundary edge (point-to-segment over consecutive corners).
    float        nearest_edge_d2 = std::numeric_limits<float>::max();
    GEO::index_t best_v0         = nearest_vertex;
    GEO::index_t best_v1         = nearest_vertex;
    for (GEO::index_t i = 0; i < corner_count; ++i) {
        const GEO::index_t corner0 = corner_begin + i;
        const GEO::index_t corner1 = corner_begin + ((i + 1) % corner_count);
        const GEO::index_t v0      = geo_mesh.facet_corners.vertex(corner0);
        const GEO::index_t v1      = geo_mesh.facet_corners.vertex(corner1);
        const glm::vec3    p0      = to_glm_vec3(get_pointf(geo_mesh.vertices, v0));
        const glm::vec3    p1      = to_glm_vec3(get_pointf(geo_mesh.vertices, v1));
        const glm::vec3    d       = p1 - p0;
        const float        len2    = glm::dot(d, d);
        const float        t       = (len2 > 0.0f) ? glm::clamp(glm::dot(hover_in_mesh - p0, d) / len2, 0.0f, 1.0f) : 0.0f;
        const glm::vec3    closest = p0 + (t * d);
        const float        d2      = glm::distance2(hover_in_mesh, closest);
        if (d2 < nearest_edge_d2) {
            nearest_edge_d2 = d2;
            best_v0         = v0;
            best_v1         = v1;
        }
    }
    result.edge_v0 = best_v0;
    result.edge_v1 = best_v1;

    return result;
}

auto Mesh_component_selection_tool::vertex_normal_local(const erhe::geometry::Geometry& geometry, const GEO::index_t vertex) const -> glm::vec3
{
    const GEO::Mesh& geo_mesh = geometry.get_mesh();
    glm::vec3        sum{0.0f};
    for (const GEO::index_t corner : geometry.get_vertex_corners(vertex)) {
        const GEO::index_t facet = geometry.get_corner_facet(corner);
        if (facet != GEO::NO_INDEX) {
            sum += to_glm_vec3(mesh_facet_normalf(geo_mesh, facet)); // area-weighted
        }
    }
    return sum;
}

auto Mesh_component_selection_tool::edge_world_normal(
    const erhe::geometry::Geometry& geometry,
    const glm::mat3&                normal_matrix,
    const GEO::index_t              v0,
    const GEO::index_t              v1
) const -> glm::vec3
{
    const glm::vec3 local = vertex_normal_local(geometry, v0) + vertex_normal_local(geometry, v1);
    const glm::vec3 world = normal_matrix * local;
    const float     len   = glm::length(world);
    return (len > 1e-6f) ? (world / len) : glm::vec3{0.0f};
}

auto Mesh_component_selection_tool::try_ready() const -> bool
{
    if (m_mesh_component_selection.get_mode() == Mesh_component_mode::object) {
        return false;
    }
    // In Paint gesture sub-mode the paint command handles clicks (one dab); the
    // single-click select must not also fire (it would double-select). Box mode
    // keeps single-click for a click-without-motion (Blender-like).
    if (m_gesture_mode == Component_gesture_mode::paint) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    return pick(*scene_view).valid;
}

auto Mesh_component_selection_tool::on_select() -> bool
{
    if (m_mesh_component_selection.get_mode() == Mesh_component_mode::object) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    const Pick_result pick_result = pick(*scene_view);
    if (!pick_result.valid) {
        return false;
    }

    Mesh_component_selection& selection = m_mesh_component_selection;

    // Plain click replaces the whole selection (single mesh); Ctrl/Shift extend
    // toggles within the picked mesh's entry and may accumulate across meshes.
    const bool extend = (m_context.input_state != nullptr) &&
                        (m_context.input_state->control || m_context.input_state->shift);
    if (!extend) {
        selection.clear_all();
    }
    Mesh_component_entry& entry = selection.find_or_create_entry(
        pick_result.mesh, pick_result.primitive_index, pick_result.geometry
    );

    switch (selection.get_mode()) {
        case Mesh_component_mode::vertex: {
            if (extend) {
                entry.toggle_vertex(pick_result.vertex);
            } else {
                entry.add_vertex(pick_result.vertex);
            }
            break;
        }
        case Mesh_component_mode::edge: {
            if (extend) {
                entry.toggle_edge(pick_result.edge_v0, pick_result.edge_v1);
            } else {
                entry.add_edge(pick_result.edge_v0, pick_result.edge_v1);
            }
            break;
        }
        case Mesh_component_mode::face: {
            if (extend) {
                entry.toggle_facet(pick_result.facet);
            } else {
                entry.add_facet(pick_result.facet);
            }
            break;
        }
        case Mesh_component_mode::object:
        default: {
            return false;
        }
    }
    return true;
}

void Mesh_component_selection_tool::append_facet_triangles(const erhe::geometry::Geometry& geometry, const GEO::index_t facet)
{
    const GEO::Mesh&   geo_mesh     = geometry.get_mesh();
    const GEO::index_t corner_begin = geo_mesh.facets.corners_begin(facet);
    const GEO::index_t corner_end   = geo_mesh.facets.corners_end(facet);
    const GEO::index_t corner_count = corner_end - corner_begin;
    if (corner_count < 3) {
        return;
    }
    const uint32_t base = static_cast<uint32_t>(m_scratch_positions.size());
    for (GEO::index_t corner = corner_begin; corner < corner_end; ++corner) {
        const GEO::index_t vertex = geo_mesh.facet_corners.vertex(corner);
        m_scratch_positions.push_back(to_glm_vec3(get_pointf(geo_mesh.vertices, vertex)));
    }
    // Triangle fan from the first corner.
    for (GEO::index_t i = 1; (i + 1) < corner_count; ++i) {
        m_scratch_indices.push_back(base);
        m_scratch_indices.push_back(base + i);
        m_scratch_indices.push_back(base + i + 1);
    }
}

void Mesh_component_selection_tool::append_vertex_quad(
    const glm::vec3& position_in_world,
    const glm::vec3& camera_right,
    const glm::vec3& camera_up,
    const float      half_size
)
{
    const glm::vec3 r        = half_size * camera_right;
    const glm::vec3 u        = half_size * camera_up;
    const uint32_t  base     = static_cast<uint32_t>(m_scratch_positions.size());
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

void Mesh_component_selection_tool::tool_render(const Render_context& context)
{
    // Desktop viewport only. viewport_scene_view is null for the headset
    // (multiview) and for preview renders, so this gate also guarantees no
    // triangle primitives are queued during a multiview frame -- the direct
    // triangle path of Debug_renderer is single-view only.
    if (context.viewport_scene_view == nullptr) {
        return;
    }
    if (m_mesh_component_selection.get_mode() == Mesh_component_mode::object) {
        return;
    }

    // stencil_reference must be non-zero: the debug pipeline's stencil test is
    // function=greater against the (zero-cleared) stencil buffer, so reference 0
    // would reject every fragment. 2 matches the other debug tools (Paint/Hover).
    erhe::renderer::Primitive_renderer triangle_renderer = context.get({erhe::graphics::Primitive_type::triangle, 2, true, false});
    // TEMP: draw_hidden set to false to isolate rendering issues in the
    // surface-aligned edge "tent" (the hidden/xray pass otherwise overlays the
    // visible pass). Restore to true once the tent is verified.
    erhe::renderer::Primitive_renderer line_renderer     = context.get({erhe::graphics::Primitive_type::line,     2, true, false});

    // Editor-global visual style (Editor_settings_config.mesh_component_style,
    // edited in the Settings window), shared by all scene views.
    const Mesh_component_style& style = context.app_context.editor_settings->mesh_component_style;

    // Surface-line depth bias is a single global on the debug renderer (it is
    // written to the view UBO when the line bucket flushes, after all tools have
    // queued). Push the per-viewport config value here so the selected-edge
    // "tent" lines bias correctly for this view.
    if (m_context.debug_renderer != nullptr) {
        m_context.debug_renderer->set_line_bias_margin(style.edge_depth_bias);
    }

    // Camera basis for billboarded vertex handles.
    glm::vec3       camera_position{0.0f};
    glm::vec3       camera_right   {1.0f, 0.0f, 0.0f};
    glm::vec3       camera_up      {0.0f, 1.0f, 0.0f};
    const erhe::scene::Node* camera_node = context.get_camera_node();
    if (camera_node != nullptr) {
        const glm::mat4 world_from_camera = camera_node->world_from_node();
        camera_position = glm::vec3{world_from_camera[3]};
        camera_right    = glm::normalize(glm::vec3{world_from_camera[0]});
        camera_up       = glm::normalize(glm::vec3{world_from_camera[1]});
    }

    // Render every live entry (multi-mesh). is_live() guarantees the mesh is in
    // the scene and the primitive still carries the exact Geometry the indices
    // address, so a geometry swap (dormant entry) or a scene removal (mesh out of
    // scene) draws nothing - no stale ghost, no poll needed.
    m_mesh_component_selection.prune();
    for (const Mesh_component_entry& entry : m_mesh_component_selection.get_entries()) {
        if (!m_mesh_component_selection.is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh>        mesh     = entry.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        const erhe::scene::Node*                        node     = mesh->get_node();

        const glm::mat4  world_from_node = node->world_from_node();
        const glm::mat3  normal_matrix   = glm::transpose(glm::inverse(glm::mat3(world_from_node)));
        const GEO::Mesh& geo_mesh        = geometry->get_mesh();

        // Faces.
        m_scratch_positions.clear();
        m_scratch_indices.clear();
        for (const GEO::index_t facet : entry.facets) {
            append_facet_triangles(*geometry, facet);
        }
        if (!m_scratch_indices.empty()) {
            triangle_renderer.add_triangles(world_from_node, style.face_color, m_scratch_positions, m_scratch_indices);
        }

        // Edges. Each carries its two adjacent face normals (plus an interior-
        // tangent sign) so the compute line shader makes each side of the
        // wide-line ribbon coplanar with its face (the two-face "tent"),
        // eliminating z-fight with both faces.
        m_scratch_lines.clear();
        m_scratch_face_normals_a.clear();
        m_scratch_face_normals_b.clear();
        m_scratch_signs_a.clear();
        for (const Mesh_edge_key& edge : entry.edges) {
            const glm::vec3 p0 = to_glm_vec3(get_pointf(geo_mesh.vertices, edge.first));
            const glm::vec3 p1 = to_glm_vec3(get_pointf(geo_mesh.vertices, edge.second));
            m_scratch_lines.push_back(erhe::renderer::Line{p0, p1});
            const Edge_surface_frame frame = compute_edge_surface_frame(*geometry, world_from_node, normal_matrix, edge.first, edge.second);
            m_scratch_face_normals_a.push_back(frame.normal_a);
            m_scratch_face_normals_b.push_back(frame.normal_b);
            m_scratch_signs_a.push_back(frame.sign_a);
        }
        if (!m_scratch_lines.empty()) {
            line_renderer.set_thickness(style.edge_thickness);
            line_renderer.add_surface_lines(
                world_from_node, style.edge_color,
                std::span<const erhe::renderer::Line>{m_scratch_lines},
                std::span<const glm::vec3>{m_scratch_face_normals_a},
                std::span<const glm::vec3>{m_scratch_face_normals_b},
                std::span<const float>{m_scratch_signs_a}
            );
        }

        // Vertices (camera-facing quads, world space).
        m_scratch_positions.clear();
        m_scratch_indices.clear();
        for (const GEO::index_t vertex : entry.vertices) {
            const glm::vec3 v_local = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
            const glm::vec3 v_world = glm::vec3{world_from_node * glm::vec4{v_local, 1.0f}};
            const float     half    = style.vertex_size * glm::distance(camera_position, v_world);
            append_vertex_quad(v_world, camera_right, camera_up, half);
        }
        if (!m_scratch_indices.empty()) {
            triangle_renderer.add_triangles(glm::mat4{1.0f}, style.vertex_color, m_scratch_positions, m_scratch_indices);
        }
    }

    // Hover highlight for the component under the pointer -- only in the view the
    // pointer is actually over. get_hover_scene_view() is fed by the
    // App_message_bus hover_scene_view message (subscribed in the constructor)
    // and becomes null when the pointer leaves every viewport, so the highlight
    // does not linger on a stale hover in the previously hovered view.
    const Pick_result hover = (get_hover_scene_view() == &context.scene_view)
        ? pick(context.scene_view)
        : Pick_result{};
    if (hover.valid) {
        const erhe::scene::Node* hover_node = hover.mesh->get_node();
        if (hover_node != nullptr) {
            const glm::mat4  world_from_node = hover_node->world_from_node();
            const GEO::Mesh& geo_mesh        = hover.geometry->get_mesh();
            switch (m_mesh_component_selection.get_mode()) {
                case Mesh_component_mode::face: {
                    m_scratch_positions.clear();
                    m_scratch_indices.clear();
                    append_facet_triangles(*hover.geometry, hover.facet);
                    if (!m_scratch_indices.empty()) {
                        triangle_renderer.add_triangles(world_from_node, style.hover_color, m_scratch_positions, m_scratch_indices);
                    }
                    break;
                }
                case Mesh_component_mode::edge: {
                    const glm::vec3 p0 = to_glm_vec3(get_pointf(geo_mesh.vertices, hover.edge_v0));
                    const glm::vec3 p1 = to_glm_vec3(get_pointf(geo_mesh.vertices, hover.edge_v1));
                    const glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(world_from_node)));
                    m_scratch_lines.clear();
                    m_scratch_normals.clear();
                    m_scratch_lines.push_back(erhe::renderer::Line{p0, p1});
                    m_scratch_normals.push_back(edge_world_normal(*hover.geometry, normal_matrix, hover.edge_v0, hover.edge_v1));
                    line_renderer.set_thickness(style.edge_thickness - 1.0f); // 1px thicker (negative = screen-space, so more negative = thicker)
                    line_renderer.add_lines(
                        world_from_node, style.hover_color,
                        std::span<const erhe::renderer::Line>{m_scratch_lines},
                        std::span<const glm::vec3>{m_scratch_normals}
                    );
                    break;
                }
                case Mesh_component_mode::vertex: {
                    const glm::vec3 v_local = to_glm_vec3(get_pointf(geo_mesh.vertices, hover.vertex));
                    const glm::vec3 v_world = glm::vec3{world_from_node * glm::vec4{v_local, 1.0f}};
                    const float     half    = (style.vertex_size * 1.3f) * glm::distance(camera_position, v_world);
                    m_scratch_positions.clear();
                    m_scratch_indices.clear();
                    append_vertex_quad(v_world, camera_right, camera_up, half);
                    triangle_renderer.add_triangles(glm::mat4{1.0f}, style.hover_color, m_scratch_positions, m_scratch_indices);
                    break;
                }
                case Mesh_component_mode::object:
                default: {
                    break;
                }
            }
        }
    }
}

void Mesh_component_selection_tool::viewport_toolbar()
{
    ImGui::PushID("Mesh_component_selection_tool::viewport_toolbar");

    Mesh_component_selection& selection = m_mesh_component_selection;

    // Mode combo (Object / Vertex / Edge / Face). In a menu bar successive
    // widgets flow horizontally, so the Clear button lands to the right.
    int               mode_index = static_cast<int>(selection.get_mode());
    const char* const items[]    = {"Object", "Vertex", "Edge", "Face"};
    if (erhe::imgui::combo_fit_width("##mesh_component_mode", &mode_index, items, IM_ARRAYSIZE(items))) {
        selection.set_mode(static_cast<Mesh_component_mode>(mode_index));
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mesh Component Selection Mode");
    }

    // Gesture sub-mode (Click / Box / Paint). Box and Paint scan the id-buffer
    // over a screen region to select faces; only meaningful in Face mode for now.
    // The B / C hotkeys switch to Box / Paint as a shortcut for this combo.
    int               gesture_index   = static_cast<int>(m_gesture_mode);
    const char* const gesture_items[] = {"Click", "Box", "Paint"};
    if (erhe::imgui::combo_fit_width("##mesh_component_gesture", &gesture_index, gesture_items, IM_ARRAYSIZE(gesture_items))) {
        m_gesture_mode = static_cast<Component_gesture_mode>(gesture_index);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Selection gesture: Click picks one face; Box drags a rectangle; Paint drags a brush (B / C hotkeys)");
    }

    // Brush radius (paint mode), in viewport pixels. Also adjustable with the
    // mouse wheel while painting.
    if (m_gesture_mode == Component_gesture_mode::paint) {
        ImGui::SetNextItemWidth(110.0f);
        ImGui::DragFloat("##brush_radius", &m_brush_radius, 1.0f, 4.0f, 512.0f, "Brush %.0f px");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Paint brush radius in pixels (mouse wheel resizes it while painting)");
        }
    }

    // Geometry edit mode (Shared / Fork): when the edited geometry is shared by
    // other meshes (e.g. a duplicate), Shared edits it in place so all instances
    // change; Fork deep-copies the geometry for this instance on the first move.
    if (m_context.editor_settings != nullptr) {
        int               edit_index    = static_cast<int>(m_context.editor_settings->geometry_edit_mode);
        const char* const edit_items[]  = {"Shared", "Fork"};
        if (erhe::imgui::combo_fit_width("##geometry_edit_mode", &edit_index, edit_items, IM_ARRAYSIZE(edit_items))) {
            m_context.editor_settings->geometry_edit_mode = static_cast<Geometry_edit_mode>(edit_index);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shared: edit shared geometry (all instances change). Fork: copy geometry on edit (only this instance changes).");
        }

        // Transform mode: how the gizmo transforms the selected components. Move
        // translates/rotates/scales them in place; Extrude duplicates the selection
        // boundary, bridges it with new faces, then moves the duplicates along the gizmo
        // delta; Extrude (Group Normal) does the same topology change but slides each
        // disjoint subset along its own average normal; Extrude (Vertex Normal) slides each
        // vertex along its own normal. The item order matches the Mesh_transform_mode enum
        // values (move, extrude, extrude_group_normal, extrude_vertex_normal).
        int               transform_index   = static_cast<int>(m_context.editor_settings->transform_mode);
        const char* const transform_items[] = {"Move", "Extrude", "Extrude (Group Normal)", "Extrude (Vertex Normal)"};
        if (erhe::imgui::combo_fit_width("##mesh_transform_mode", &transform_index, transform_items, IM_ARRAYSIZE(transform_items))) {
            m_context.editor_settings->transform_mode = static_cast<Mesh_transform_mode>(transform_index);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move: drag moves the selected components. Extrude: drag extrudes them (new faces) then moves. Extrude (Group Normal): extrudes, then each disjoint subset slides along its own average normal by the drag amount. Extrude (Vertex Normal): extrudes, then each vertex slides along its own normal.");
        }
    }

    if (ImGui::Button("Clear")) {
        selection.clear_all();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear mesh component selection");
    }

    ImGui::PopID();

    // Visual style (colors, edge thickness, vertex size, edge depth bias) is
    // edited in the Settings window; it is stored editor-global in
    // Editor_settings_config::mesh_component_style so codegen serialization /
    // autosave cover it.
}

namespace {

// Map each scanned (mesh, primitive, triangle) hit to a facet and add it to (or
// erase it from) the component selection. Shared by box-commit and paint-apply.
// Applies the single-click path's rejections (skinned / edit-locked meshes,
// missing triangle->facet mapping).
void apply_scan_hits_to_selection(
    Mesh_component_selection&         selection,
    const Id_renderer::Scan_result&   result,
    const bool                        subtract
)
{
    for (const Id_renderer::Scan_hit& hit : result.hits) {
        const std::shared_ptr<erhe::scene::Mesh> mesh = hit.mesh;
        if (!mesh) {
            continue;
        }
        if (mesh->skin) {
            continue; // skinned meshes deform on the GPU; CPU geometry does not match
        }
        if (mesh->is_lock_edit()) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        if (hit.primitive_index >= primitives.size()) {
            continue;
        }
        const erhe::scene::Mesh_primitive& mesh_primitive = primitives[hit.primitive_index];
        if (!mesh_primitive.primitive) {
            continue;
        }
        const erhe::primitive::Primitive&                       primitive = *mesh_primitive.primitive.get();
        const std::shared_ptr<erhe::primitive::Primitive_shape> shape     = primitive.get_shape_for_raytrace();
        if (!shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry> geometry = shape->get_geometry();
        if (!geometry) {
            continue;
        }
        const GEO::index_t facet = shape->get_mesh_facet_from_triangle(static_cast<uint32_t>(hit.triangle_id));
        if (facet == GEO::NO_INDEX) {
            continue;
        }
        Mesh_component_entry& entry = selection.find_or_create_entry(mesh, hit.primitive_index, geometry);
        if (subtract) {
            entry.facets.erase(facet);
        } else {
            entry.add_facet(facet);
        }
    }
}

} // anonymous namespace

#pragma region Gesture selection (box / paint)
auto Mesh_component_selection_tool::get_gesture_mode() const -> Component_gesture_mode
{
    return m_gesture_mode;
}

void Mesh_component_selection_tool::set_gesture_mode(const Component_gesture_mode mode)
{
    m_gesture_mode = mode;
}

auto Mesh_component_selection_tool::try_set_gesture_hotkey(const Component_gesture_mode mode) -> bool
{
    // Only act (and consume the key) while in Face component mode; otherwise the
    // key falls through to other bindings (e.g. brush preview on C).
    if (m_mesh_component_selection.get_mode() != Mesh_component_mode::face) {
        return false;
    }
    m_gesture_mode = mode;
    return true;
}

auto Mesh_component_selection_tool::box_select_try_ready() const -> bool
{
    if (m_gesture_mode != Component_gesture_mode::box) {
        return false;
    }
    if (m_mesh_component_selection.get_mode() != Mesh_component_mode::face) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    // Desktop viewport only (the id-buffer scan needs a Viewport_scene_view).
    return scene_view->as_viewport_scene_view() != nullptr;
}

auto Mesh_component_selection_tool::is_gesture_box_mode() const -> bool
{
    return (m_gesture_mode == Component_gesture_mode::box) &&
           (m_mesh_component_selection.get_mode() == Mesh_component_mode::face);
}

void Mesh_component_selection_tool::box_select_update(const glm::vec2 window_position, const bool real_motion)
{
    if (real_motion) {
        if (!m_box_active) {
            // Drag just started: anchor here, lock to the starting view, and
            // drop any commit still pending from a previous box.
            m_box_active         = true;
            m_box_scene_view     = get_hover_scene_view();
            m_box_anchor_window  = window_position;
            m_box_commit_pending = false;
        }
        m_box_current_window = window_position;
    }
    if (m_box_active) {
        request_box_scan();
    }
}

void Mesh_component_selection_tool::request_box_scan()
{
    if ((m_box_scene_view == nullptr) || (m_context.id_renderer == nullptr)) {
        return;
    }
    const Viewport_scene_view* viewport_scene_view = m_box_scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return;
    }
    const glm::vec2 a = viewport_scene_view->get_viewport_from_window(m_box_anchor_window);
    const glm::vec2 b = viewport_scene_view->get_viewport_from_window(m_box_current_window);
    const int x0 = static_cast<int>(std::floor(std::min(a.x, b.x)));
    const int y0 = static_cast<int>(std::floor(std::min(a.y, b.y)));
    const int x1 = static_cast<int>(std::ceil (std::max(a.x, b.x)));
    const int y1 = static_cast<int>(std::ceil (std::max(a.y, b.y)));
    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }
    Id_renderer::Scan_request request;
    request.x        = x0;
    request.y        = y0;
    request.width    = x1 - x0;
    request.height   = y1 - y0;
    request.is_brush = false;
    m_context.id_renderer->request_scan(request);
}

void Mesh_component_selection_tool::box_select_release()
{
    if (!m_box_active) {
        return; // click without motion -> handled by the single-click command
    }
    m_box_active = false;
    // Capture modifiers at release (Blender: plain = replace, Shift = add,
    // Ctrl = subtract).
    m_box_modifier_shift = (m_context.input_state != nullptr) && m_context.input_state->shift;
    m_box_modifier_ctrl  = (m_context.input_state != nullptr) && m_context.input_state->control;
    m_box_commit_pending       = true;
    m_box_commit_request_frame = 0; // a fresh post-release scan is requested in gesture_update()
}

auto Mesh_component_selection_tool::paint_select_try_ready() const -> bool
{
    if (m_gesture_mode != Component_gesture_mode::paint) {
        return false;
    }
    if (m_mesh_component_selection.get_mode() != Mesh_component_mode::face) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    return scene_view->as_viewport_scene_view() != nullptr;
}

auto Mesh_component_selection_tool::is_gesture_paint_mode() const -> bool
{
    return (m_gesture_mode == Component_gesture_mode::paint) &&
           (m_mesh_component_selection.get_mode() == Mesh_component_mode::face);
}

void Mesh_component_selection_tool::paint_select_update(const glm::vec2 window_position, const bool real_motion)
{
    if (real_motion) {
        if (!m_paint_active) {
            // Stroke just started: lock to the starting view, capture modifiers,
            // and (for a plain stroke) clear the selection once before adding.
            m_paint_active             = true;
            m_paint_pending            = true;
            m_paint_scene_view         = get_hover_scene_view();
            m_paint_last_applied_frame = 0;
            const bool shift = (m_context.input_state != nullptr) && m_context.input_state->shift;
            m_paint_subtract = (m_context.input_state != nullptr) && m_context.input_state->control;
            if (!m_paint_subtract && !shift) {
                m_mesh_component_selection.clear_all();
            }
        }
        m_brush_center_window = window_position;
    }
    if (m_paint_active) {
        request_paint_scan();
    }
}

void Mesh_component_selection_tool::request_paint_scan()
{
    if ((m_paint_scene_view == nullptr) || (m_context.id_renderer == nullptr)) {
        return;
    }
    const Viewport_scene_view* viewport_scene_view = m_paint_scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return;
    }
    const glm::vec2 center = viewport_scene_view->get_viewport_from_window(m_brush_center_window);
    const float     radius = m_brush_radius;
    Id_renderer::Scan_request request;
    request.x            = static_cast<int>(std::floor(center.x - radius));
    request.y            = static_cast<int>(std::floor(center.y - radius));
    request.width        = static_cast<int>(std::ceil (2.0f * radius)) + 1;
    request.height       = static_cast<int>(std::ceil (2.0f * radius)) + 1;
    request.is_brush     = true;
    request.brush_center = center;
    request.brush_radius = radius;
    m_context.id_renderer->request_scan(request);
    if (m_context.graphics_device != nullptr) {
        m_paint_last_request_frame = m_context.graphics_device->get_frame_index();
    }
}

void Mesh_component_selection_tool::paint_select_release()
{
    if (!m_paint_active) {
        return;
    }
    m_paint_active = false; // m_paint_pending stays set so gesture_update drains the last results
}

void Mesh_component_selection_tool::adjust_brush_radius(const float wheel_delta)
{
    if (wheel_delta == 0.0f) {
        return;
    }
    // Wheel up grows the brush, ~10% per notch.
    const float factor = std::pow(1.1f, wheel_delta);
    m_brush_radius = std::clamp(m_brush_radius * factor, 4.0f, 512.0f);
}

void Mesh_component_selection_tool::gesture_update()
{
    const bool have_devices = (m_context.id_renderer != nullptr) && (m_context.graphics_device != nullptr);

    // Debug/test (MCP debug_region_select): drive a region scan over an explicit
    // viewport rectangle / disk and commit when it completes. Exclusive with the
    // mouse gestures below (only one is ever pending at a time).
    if (m_debug_pending && have_devices) {
        Id_renderer::Scan_request request;
        request.x        = m_debug_x;
        request.y        = m_debug_y;
        request.width    = m_debug_w;
        request.height   = m_debug_h;
        request.is_brush = m_debug_is_brush;
        if (m_debug_is_brush) {
            request.brush_center = glm::vec2{static_cast<float>(m_debug_x) + 0.5f * static_cast<float>(m_debug_w),
                                             static_cast<float>(m_debug_y) + 0.5f * static_cast<float>(m_debug_h)};
            request.brush_radius = m_debug_brush_radius;
        }
        m_context.id_renderer->request_scan(request);
        if (m_debug_request_frame == 0) {
            m_debug_request_frame = m_context.graphics_device->get_frame_index();
        }
        const Id_renderer::Scan_result& result = m_context.id_renderer->take_scan_result();
        if (result.ready && (result.frame_number >= m_debug_request_frame)) {
            if (m_debug_replace) {
                m_mesh_component_selection.clear_all();
            }
            apply_scan_hits_to_selection(m_mesh_component_selection, result, m_debug_subtract);
            m_debug_pending = false;
        }
    }

    // Box: deferred single commit after release, once a scan whose pixels are
    // from at-or-after the first post-release request completes.
    if (m_box_commit_pending) {
        if (
            !have_devices ||
            (m_gesture_mode != Component_gesture_mode::box) ||
            (m_mesh_component_selection.get_mode() != Mesh_component_mode::face)
        ) {
            m_box_commit_pending = false;
        } else {
            // Re-request the final box every frame until its scan completes; this
            // guards against all region slots being busy on the release frame.
            request_box_scan();
            if (m_box_commit_request_frame == 0) {
                m_box_commit_request_frame = m_context.graphics_device->get_frame_index();
            }
            const Id_renderer::Scan_result& result = m_context.id_renderer->take_scan_result();
            if (result.ready && (result.frame_number >= m_box_commit_request_frame)) {
                const bool replace  = !m_box_modifier_shift && !m_box_modifier_ctrl;
                const bool subtract = m_box_modifier_ctrl;
                if (replace) {
                    m_mesh_component_selection.clear_all();
                }
                apply_scan_hits_to_selection(m_mesh_component_selection, result, subtract);
                m_box_commit_pending = false;
            }
        }
    }

    // Paint: apply scan results continuously while painting, and drain the last
    // results for a few frames after release.
    if (m_paint_pending) {
        if (!have_devices) {
            m_paint_pending = false;
        } else {
            // After release, re-request the final brush so its faces are not
            // missed if the release-frame scan was dropped (all slots busy).
            if (!m_paint_active) {
                request_paint_scan();
            }
            const Id_renderer::Scan_result& result = m_context.id_renderer->take_scan_result();
            if (result.ready && (result.frame_number > m_paint_last_applied_frame)) {
                apply_scan_hits_to_selection(m_mesh_component_selection, result, m_paint_subtract);
                m_paint_last_applied_frame = result.frame_number;
            }
            if (!m_paint_active && (m_paint_last_applied_frame >= m_paint_last_request_frame)) {
                m_paint_pending = false;
            }
        }
    }

    // Keep the brush-radius wheel command Ready while paint-selecting + hovering
    // a viewport, so it out-ranks the fly-camera zoom for the wheel (priority is
    // state-dominated, and sort_mouse_wheel_bindings orders by priority). It is
    // set Inactive otherwise so the wheel zooms the camera as usual.
    Scene_view* const hover_scene_view = get_hover_scene_view();
    const bool paint_wheel_active =
        (m_gesture_mode == Component_gesture_mode::paint) &&
        (m_mesh_component_selection.get_mode() == Mesh_component_mode::face) &&
        (hover_scene_view != nullptr) &&
        (hover_scene_view->as_viewport_scene_view() != nullptr);
    if (paint_wheel_active) {
        m_brush_radius_command.set_ready();
    } else {
        m_brush_radius_command.set_inactive();
    }
}

void Mesh_component_selection_tool::debug_region_select(
    const int   x,
    const int   y,
    const int   width,
    const int   height,
    const bool  is_brush,
    const float brush_radius,
    const bool  replace,
    const bool  subtract
)
{
    m_debug_x             = x;
    m_debug_y             = y;
    m_debug_w             = width;
    m_debug_h             = height;
    m_debug_is_brush      = is_brush;
    m_debug_brush_radius  = brush_radius;
    m_debug_replace       = replace;
    m_debug_subtract      = subtract;
    m_debug_request_frame = 0;
    m_debug_pending       = true;
}

void Mesh_component_selection_tool::draw_gesture_overlay(const Viewport_scene_view* viewport_scene_view)
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    if (draw_list == nullptr) {
        return;
    }
    const ImU32 fill_color = IM_COL32(60, 130, 220, 40);
    const ImU32 line_color = IM_COL32(60, 130, 220, 220);

    // Box rubber-band, drawn only in the view the gesture started in.
    if (
        (m_gesture_mode == Component_gesture_mode::box) &&
        m_box_active &&
        (m_box_scene_view != nullptr) &&
        (m_box_scene_view->as_viewport_scene_view() == viewport_scene_view)
    ) {
        const ImVec2 p0{m_box_anchor_window.x,  m_box_anchor_window.y};
        const ImVec2 p1{m_box_current_window.x, m_box_current_window.y};
        draw_list->AddRectFilled(p0, p1, fill_color);
        draw_list->AddRect      (p0, p1, line_color, 0.0f, ImDrawFlags_None, 1.5f);
    }

    // Brush circle, shown in Paint mode. While painting, draw at the brush
    // centre in the stroke's view; while only hovering, draw at the cursor in
    // the hovered view.
    if (m_gesture_mode == Component_gesture_mode::paint) {
        bool   draw_brush = false;
        ImVec2 center{0.0f, 0.0f};
        if (m_paint_active) {
            if ((m_paint_scene_view != nullptr) && (m_paint_scene_view->as_viewport_scene_view() == viewport_scene_view)) {
                center     = ImVec2{m_brush_center_window.x, m_brush_center_window.y};
                draw_brush = true;
            }
        } else {
            Scene_view* const hover_scene_view = get_hover_scene_view();
            if ((hover_scene_view != nullptr) && (hover_scene_view->as_viewport_scene_view() == viewport_scene_view)) {
                center     = ImGui::GetMousePos();
                draw_brush = true;
            }
        }
        if (draw_brush) {
            draw_list->AddCircleFilled(center, m_brush_radius, fill_color);
            draw_list->AddCircle      (center, m_brush_radius, line_color, 0, 1.5f);
        }
    }
}
#pragma endregion Gesture selection (box / paint)

} // namespace editor
