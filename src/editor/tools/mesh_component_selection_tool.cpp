#include "tools/mesh_component_selection_tool.hpp"

#include "tools/mesh_component_selection.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_settings.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "config/generated/viewport_config.hpp"
#include "editor_log.hpp"
#include "graphics/gradients.hpp"
#include "input_state.hpp"
#include "operations/compound_operation.hpp"
#include "operations/fork_geometry_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "operations/operation_stack.hpp"
#include "operations/set_edge_sharpness_operation.hpp"
#include "renderers/id_renderer.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "transform/transform_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_commands/input_arguments.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/operation/subdivide_edges.hpp"
#include "erhe_item/item_host.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_utility/bit_helpers.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include "erhe_imgui/imgui_helpers.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>
#include <string>

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

// Only meshes flagged as scene content are valid mesh-component-selection
// targets. Tool / brush / controller / rendertarget / id meshes (and any other
// non-content mesh) must never be selectable as components, nor show a hover
// highlight. Used by both the single-click/hover pick() and the box/paint scan.
[[nodiscard]] auto is_content_mesh(const erhe::scene::Mesh& mesh) -> bool
{
    return erhe::utility::test_bit_set(mesh.get_flag_bits(), erhe::Item_flags::content);
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

Component_grow_selection_command::Component_grow_selection_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.grow"}
    , m_context{context}
{
}

auto Component_grow_selection_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->grow_selection();
}

Component_shrink_selection_command::Component_shrink_selection_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.shrink"}
    , m_context{context}
{
}

auto Component_shrink_selection_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->shrink_selection();
}

auto c_str(const Component_selection_action action) -> const char*
{
    switch (action) {
        case Component_selection_action::select_all:                   return "Mesh_component_selection.select_all";
        case Component_selection_action::select_none:                  return "Mesh_component_selection.select_none";
        case Component_selection_action::invert:                       return "Mesh_component_selection.invert";
        case Component_selection_action::select_linked_under_cursor:   return "Mesh_component_selection.select_linked_under_cursor";
        case Component_selection_action::select_linked_from_selection: return "Mesh_component_selection.select_linked_from_selection";
        default:                                                       return "?";
    }
}

Component_selection_action_command::Component_selection_action_command(
    erhe::commands::Commands&        commands,
    App_context&                     context,
    const Component_selection_action action
)
    : Command  {commands, c_str(action)}
    , m_context{context}
    , m_action {action}
{
}

auto Component_selection_action_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->run_selection_action(m_action);
}
Component_loop_select_command::Component_loop_select_command(
    erhe::commands::Commands& commands,
    App_context&              context,
    const char*               name,
    const Loop_select_gesture gesture
)
    : Command  {commands, name}
    , m_context{context}
    , m_gesture{gesture}
{
}

void Component_loop_select_command::try_ready()
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return;
    }
    if (m_context.mesh_component_selection_tool->try_ready()) {
        set_ready();
    }
}

auto Component_loop_select_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    if (get_command_state() != erhe::commands::State::Ready) {
        return false;
    }
    if (m_context.mesh_component_selection_tool == nullptr) {
        set_inactive();
        return false;
    }
    const bool consumed = m_context.mesh_component_selection_tool->on_loop_select(m_gesture, input.modifier_mask);
    set_inactive();
    return consumed;
}

Component_slide_command::Component_slide_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.slide"}
    , m_context{context}
{
}

auto Component_slide_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->begin_slide();
}

auto c_str(const Component_modal_action action) -> const char*
{
    switch (action) {
        case Component_modal_action::confirm:        return "confirm";
        case Component_modal_action::cancel:         return "cancel";
        case Component_modal_action::toggle_even:    return "toggle_even";
        case Component_modal_action::toggle_flipped: return "toggle_flipped";
        case Component_modal_action::toggle_clamp:   return "toggle_clamp";
        default:                                     return "?";
    }
}

Component_modal_command::Component_modal_command(
    erhe::commands::Commands&    commands,
    App_context&                 context,
    const char*                  name,
    const Component_modal_action action
)
    : Command  {commands, name}
    , m_context{context}
    , m_action {action}
{
}

void Component_modal_command::try_ready()
{
    if ((m_context.mesh_component_selection_tool != nullptr) && m_context.mesh_component_selection_tool->is_modal_active()) {
        set_ready();
    }
}

auto Component_modal_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->run_modal_action(m_action);
}

auto c_str(const Loop_cut_action action) -> const char*
{
    switch (action) {
        case Loop_cut_action::start:           return "start";
        case Loop_cut_action::more_cuts:       return "more_cuts";
        case Loop_cut_action::fewer_cuts:      return "fewer_cuts";
        case Loop_cut_action::more_smoothness: return "more_smoothness";
        case Loop_cut_action::less_smoothness: return "less_smoothness";
        case Loop_cut_action::type_digit:      return "type_digit";
        default:                               return "?";
    }
}

Component_loop_cut_command::Component_loop_cut_command(
    erhe::commands::Commands& commands,
    App_context&              context,
    const char*               name,
    const Loop_cut_action     action,
    const int                 digit
)
    : Command  {commands, name}
    , m_context{context}
    , m_action {action}
    , m_digit  {digit}
{
}

auto Component_loop_cut_command::try_call() -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    if (m_action == Loop_cut_action::start) {
        return m_context.mesh_component_selection_tool->begin_loop_cut();
    }
    return m_context.mesh_component_selection_tool->run_loop_cut_action(m_action, m_digit);
}

Component_loop_cut_wheel_command::Component_loop_cut_wheel_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "Mesh_component_selection.loop_cut_wheel"}
    , m_context{context}
{
}

auto Component_loop_cut_wheel_command::try_call_with_input(erhe::commands::Input_arguments& input) -> bool
{
    if (m_context.mesh_component_selection_tool == nullptr) {
        return false;
    }
    return m_context.mesh_component_selection_tool->adjust_loop_cut_wheel(input.variant.vector2.relative_value.y, input.modifier_mask);
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
    , m_grow_selection_command  {commands, context}
    , m_shrink_selection_command{commands, context}
    , m_select_all_command                  {commands, context, Component_selection_action::select_all}
    , m_select_none_command                 {commands, context, Component_selection_action::select_none}
    , m_invert_command                      {commands, context, Component_selection_action::invert}
    , m_select_linked_under_cursor_command  {commands, context, Component_selection_action::select_linked_under_cursor}
    , m_select_linked_from_selection_command{commands, context, Component_selection_action::select_linked_from_selection}
    , m_loop_select_command                 {commands, context, "Mesh_component_selection.loop_select", Loop_select_gesture::loop}
    , m_ring_select_command                 {commands, context, "Mesh_component_selection.ring_select", Loop_select_gesture::ring}
    , m_slide_command                       {commands, context}
    , m_modal_confirm_command               {commands, context, "Mesh_component_selection.modal_confirm",        Component_modal_action::confirm}
    , m_modal_cancel_command                {commands, context, "Mesh_component_selection.modal_cancel",         Component_modal_action::cancel}
    , m_modal_toggle_even_command           {commands, context, "Mesh_component_selection.modal_toggle_even",    Component_modal_action::toggle_even}
    , m_modal_toggle_flipped_command        {commands, context, "Mesh_component_selection.modal_toggle_flipped", Component_modal_action::toggle_flipped}
    , m_modal_toggle_clamp_command          {commands, context, "Mesh_component_selection.modal_toggle_clamp",   Component_modal_action::toggle_clamp}
    , m_modal_confirm_click_command         {commands, context, "Mesh_component_selection.modal_confirm_click",  Component_modal_action::confirm}
    , m_modal_cancel_click_command          {commands, context, "Mesh_component_selection.modal_cancel_click",   Component_modal_action::cancel}
    , m_loop_cut_command                    {commands, context, "Mesh_component_selection.loop_cut",                 Loop_cut_action::start,           0}
    , m_loop_cut_more_cuts_command          {commands, context, "Mesh_component_selection.loop_cut_more_cuts",       Loop_cut_action::more_cuts,       0}
    , m_loop_cut_fewer_cuts_command         {commands, context, "Mesh_component_selection.loop_cut_fewer_cuts",      Loop_cut_action::fewer_cuts,      0}
    , m_loop_cut_more_smoothness_command    {commands, context, "Mesh_component_selection.loop_cut_more_smoothness", Loop_cut_action::more_smoothness, 0}
    , m_loop_cut_less_smoothness_command    {commands, context, "Mesh_component_selection.loop_cut_less_smoothness", Loop_cut_action::less_smoothness, 0}
    , m_loop_cut_wheel_command              {commands, context}
{
    set_base_priority(c_priority);
    set_description  ("Mesh Component Selection");

    m_select_command.set_host(this);
    commands.register_command            (&m_select_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::window::Mouse_button_left, erhe::commands::Button_trigger::Button_released);

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

    // B -> Box, C -> Paint (shortcuts for the gesture combo). Gated to mesh
    // component modes in try_set_gesture_hotkey, so C still falls through to brush preview etc.
    m_box_hotkey_command.set_host(this);
    commands.register_command(&m_box_hotkey_command);
    commands.bind_command_to_key(&m_box_hotkey_command, erhe::window::Key_b);

    m_paint_hotkey_command.set_host(this);
    commands.register_command(&m_paint_hotkey_command);
    commands.bind_command_to_key(&m_paint_hotkey_command, erhe::window::Key_c);

    // Blender Select More / Select Less. Bound to both the numpad +/- (Blender's
    // exact keys) and the main-row =/- (the '+' lives on '='), so it works on
    // keyboards without a numpad. Gated to component modes in grow_selection /
    // shrink_selection so the key falls through in Object mode.
    m_grow_selection_command.set_host(this);
    commands.register_command(&m_grow_selection_command);
    commands.bind_command_to_key(&m_grow_selection_command, erhe::window::Key_kp_add, erhe::commands::Button_trigger::Button_pressed, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key(&m_grow_selection_command, erhe::window::Key_equal,  erhe::commands::Button_trigger::Button_pressed, erhe::window::Key_modifier_bit_ctrl);

    m_shrink_selection_command.set_host(this);
    commands.register_command(&m_shrink_selection_command);
    commands.bind_command_to_key(&m_shrink_selection_command, erhe::window::Key_kp_subtract, erhe::commands::Button_trigger::Button_pressed, erhe::window::Key_modifier_bit_ctrl);
    commands.bind_command_to_key(&m_shrink_selection_command, erhe::window::Key_minus,       erhe::commands::Button_trigger::Button_pressed, erhe::window::Key_modifier_bit_ctrl);

    // Blender selection keys. Each consumes the key only in a component mode.
    // Ctrl+A / Alt+A carry a modifier mask, so they dispatch before the fly
    // camera's mask-less A (erhe::commands orders masked key bindings first).
    using erhe::commands::Button_trigger;
    const std::pair<Component_selection_action_command*, std::pair<erhe::window::Keycode, uint32_t>> selection_keys[] = {
        {&m_select_all_command,                   {erhe::window::Key_a, erhe::window::Key_modifier_bit_ctrl}},
        {&m_select_none_command,                  {erhe::window::Key_a, erhe::window::Key_modifier_bit_menu}},
        {&m_invert_command,                       {erhe::window::Key_i, erhe::window::Key_modifier_bit_ctrl}},
        {&m_select_linked_under_cursor_command,   {erhe::window::Key_l, 0u}},
        {&m_select_linked_from_selection_command, {erhe::window::Key_l, erhe::window::Key_modifier_bit_ctrl}}
    };
    for (const auto& [command, key] : selection_keys) {
        command->set_host(this);
        commands.register_command(command);
        commands.bind_command_to_key(command, key.first, Button_trigger::Button_pressed, key.second);
    }

    // Loop select (Alt+click) and ring select (Ctrl+Alt+click), each with and
    // without Shift (doc/plans/mesh_modeling.md D7). A mouse button binding
    // with a modifier mask matches that mask exactly, and at equal priority
    // the masked bindings dispatch before the mask-less single-click select.
    // The fly camera's Alt drags are on the right and middle buttons.
    const uint32_t alt = erhe::window::Key_modifier_bit_menu;
    const std::pair<Component_loop_select_command*, uint32_t> loop_select_buttons[] = {
        {&m_loop_select_command, alt},
        {&m_loop_select_command, alt | erhe::window::Key_modifier_bit_shift},
        {&m_ring_select_command, alt | erhe::window::Key_modifier_bit_ctrl},
        {&m_ring_select_command, alt | erhe::window::Key_modifier_bit_ctrl | erhe::window::Key_modifier_bit_shift}
    };
    m_loop_select_command.set_host(this);
    m_ring_select_command.set_host(this);
    commands.register_command(&m_loop_select_command);
    commands.register_command(&m_ring_select_command);
    for (const auto& [command, modifier_mask] : loop_select_buttons) {
        commands.bind_command_to_mouse_button(command, erhe::window::Mouse_button_left, Button_trigger::Button_released, modifier_mask);
    }

    // Slide (doc/editor/transform.md "Scalar edits"): G starts it, and while
    // it runs Enter / left click confirm, Escape / right click cancel, E / F /
    // C toggle even / flipped / clamp. The keys carry the exact mask 0 (no
    // modifier) so they dispatch before the mask-less bindings of the same
    // keys (fly camera E, frame F, paint gesture C, log pause Escape), which
    // they fall through to while no slide runs. The mouse buttons take any
    // modifiers: Alt (unclamped) may be held at the click.
    const std::pair<Component_modal_command*, erhe::window::Keycode> modal_keys[] = {
        {&m_modal_confirm_command,        erhe::window::Key_enter},
        {&m_modal_confirm_command,        erhe::window::Key_kp_enter},
        {&m_modal_cancel_command,         erhe::window::Key_escape},
        {&m_modal_toggle_even_command,    erhe::window::Key_e},
        {&m_modal_toggle_flipped_command, erhe::window::Key_f},
        {&m_modal_toggle_clamp_command,   erhe::window::Key_c}
    };
    m_slide_command.set_host(this);
    commands.register_command(&m_slide_command);
    commands.bind_command_to_key(&m_slide_command, erhe::window::Key_g, Button_trigger::Button_pressed, 0u);
    for (Component_modal_command* command : {
        &m_modal_confirm_command, &m_modal_cancel_command, &m_modal_toggle_even_command,
        &m_modal_toggle_flipped_command, &m_modal_toggle_clamp_command,
        &m_modal_confirm_click_command, &m_modal_cancel_click_command
    }) {
        command->set_host(this);
        commands.register_command(command);
    }
    for (const auto& [command, key] : modal_keys) {
        commands.bind_command_to_key(command, key, Button_trigger::Button_pressed, 0u);
    }
    commands.bind_command_to_mouse_button(&m_modal_confirm_click_command, erhe::window::Mouse_button_left,  Button_trigger::Button_pressed);
    commands.bind_command_to_mouse_button(&m_modal_cancel_click_command,  erhe::window::Mouse_button_right, Button_trigger::Button_pressed);

    // Loop cut (doc/editor/mesh_modeling.md): Ctrl+R starts it; while it runs
    // PageUp / PageDown / numpad plus / minus change the cut count (with Alt
    // the smoothness), digits type the count and the wheel does either. The
    // keys carry exact masks so they dispatch before the mask-less bindings
    // of the same keys (fly camera PageUp / PageDown, hotbar digits), which
    // they fall through to while the mode does not run. Enter / Escape and
    // the clicks are the modal commands above.
    const uint32_t alt_mask = erhe::window::Key_modifier_bit_menu;
    const std::pair<Component_loop_cut_command*, std::pair<erhe::window::Keycode, uint32_t>> loop_cut_keys[] = {
        {&m_loop_cut_command,                 {erhe::window::Key_r,           erhe::window::Key_modifier_bit_ctrl}},
        {&m_loop_cut_more_cuts_command,       {erhe::window::Key_page_up,     0u}},
        {&m_loop_cut_more_cuts_command,       {erhe::window::Key_kp_add,      0u}},
        {&m_loop_cut_fewer_cuts_command,      {erhe::window::Key_page_down,   0u}},
        {&m_loop_cut_fewer_cuts_command,      {erhe::window::Key_kp_subtract, 0u}},
        {&m_loop_cut_more_smoothness_command, {erhe::window::Key_page_up,     alt_mask}},
        {&m_loop_cut_more_smoothness_command, {erhe::window::Key_kp_add,      alt_mask}},
        {&m_loop_cut_less_smoothness_command, {erhe::window::Key_page_down,   alt_mask}},
        {&m_loop_cut_less_smoothness_command, {erhe::window::Key_kp_subtract, alt_mask}}
    };
    for (Component_loop_cut_command* command : {
        &m_loop_cut_command, &m_loop_cut_more_cuts_command, &m_loop_cut_fewer_cuts_command,
        &m_loop_cut_more_smoothness_command, &m_loop_cut_less_smoothness_command
    }) {
        command->set_host(this);
        commands.register_command(command);
    }
    for (const auto& [command, key] : loop_cut_keys) {
        commands.bind_command_to_key(command, key.first, Button_trigger::Button_pressed, key.second);
    }
    static constexpr const char* c_loop_cut_digit_names[10] = {
        "Mesh_component_selection.loop_cut_digit_0", "Mesh_component_selection.loop_cut_digit_1",
        "Mesh_component_selection.loop_cut_digit_2", "Mesh_component_selection.loop_cut_digit_3",
        "Mesh_component_selection.loop_cut_digit_4", "Mesh_component_selection.loop_cut_digit_5",
        "Mesh_component_selection.loop_cut_digit_6", "Mesh_component_selection.loop_cut_digit_7",
        "Mesh_component_selection.loop_cut_digit_8", "Mesh_component_selection.loop_cut_digit_9"
    };
    m_loop_cut_digit_commands.reserve(10);
    for (int digit = 0; digit < 10; ++digit) {
        m_loop_cut_digit_commands.push_back(
            std::make_unique<Component_loop_cut_command>(commands, context, c_loop_cut_digit_names[digit], Loop_cut_action::type_digit, digit)
        );
        Component_loop_cut_command* const command = m_loop_cut_digit_commands.back().get();
        command->set_host(this);
        commands.register_command(command);
        commands.bind_command_to_key(command, static_cast<erhe::window::Keycode>(erhe::window::Key_0    + digit), Button_trigger::Button_pressed, 0u);
        commands.bind_command_to_key(command, static_cast<erhe::window::Keycode>(erhe::window::Key_kp_0 + digit), Button_trigger::Button_pressed, 0u);
    }
    m_loop_cut_wheel_command.set_host(this);
    commands.register_command           (&m_loop_cut_wheel_command);
    commands.bind_command_to_mouse_wheel(&m_loop_cut_wheel_command);

    m_hover_scene_view_subscription = app_message_bus.hover_scene_view.subscribe(
        [this](Hover_scene_view_message& message) {
            Tool::on_message(message);
            update_loop_preview();
            update_loop_cut_preview();
        }
    );
    // The loop and loop cut previews follow the hover: Hover_mesh_message is
    // sent when the hovered mesh or the pointer ray changes.
    m_hover_mesh_subscription = app_message_bus.hover_mesh.subscribe(
        [this](Hover_mesh_message&) {
            update_loop_preview();
            update_loop_cut_preview();
        }
    );
    m_mode_changed_subscription = app_message_bus.mesh_component_mode_changed.subscribe(
        [this](Mesh_component_mode_changed_message&) {
            invalidate_loop_preview();
            // Leaving the component modes ends the loop cut mode.
            if (m_loop_cut.active && !is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
                end_loop_cut();
            }
            invalidate_loop_cut_preview();
        }
    );
    m_mesh_geometry_changed_subscription = app_message_bus.mesh_geometry_changed.subscribe(
        [this](Mesh_geometry_changed_message&) {
            invalidate_loop_preview();
            invalidate_loop_cut_preview();
        }
    );
}

auto Mesh_component_selection_tool::get_loop_delimit() const -> erhe::geometry::Edge_loop_delimit
{
    return m_loop_delimit_crease
        ? (erhe::geometry::Edge_loop_delimit::outer_corners | erhe::geometry::Edge_loop_delimit::crease)
        : erhe::geometry::Edge_loop_delimit::outer_corners;
}

auto Mesh_component_selection_tool::on_loop_select(const Loop_select_gesture gesture, const uint32_t modifier_mask) -> bool
{
    Mesh_component_selection& selection = m_mesh_component_selection;
    const Mesh_component_mode mode      = selection.get_mode();
    if (!is_mesh_component_mode(mode)) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    const Pick_result pick_result = pick(*scene_view);
    if (!pick_result.valid || (pick_result.edge_v0 == pick_result.edge_v1)) {
        return false;
    }
    const erhe::geometry::Geometry& geometry = *pick_result.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        log_selection->warn("Loop select: geometry '{}' has no connectivity; skipped", geometry.get_name());
        return true;
    }

    const Mesh_component_target target{
        .mesh            = pick_result.mesh,
        .primitive_index = pick_result.primitive_index,
        .geometry        = pick_result.geometry
    };
    const Mesh_edge_key                     edge_key = make_edge_key(pick_result.edge_v0, pick_result.edge_v1);
    const erhe::geometry::Edge_loop_delimit delimit  = get_loop_delimit();
    const bool                              shift    = (modifier_mask & erhe::window::Key_modifier_bit_shift) != 0;
    Loop_kind kind = (mode == Mesh_component_mode::face)
        ? Loop_kind::face_loop
        : ((gesture == Loop_select_gesture::ring) ? Loop_kind::edge_ring : Loop_kind::edge_loop);

    // Boundary cycle: a plain Alt+click on a boundary edge whose loop is
    // already selected selects the whole boundary loop; the next click on
    // the same edge returns to the loop.
    const GEO::index_t edge        = geometry.get_edge(edge_key.first, edge_key.second);
    const bool         is_boundary = (edge != GEO::NO_EDGE) && (geometry.get_edge_facets(edge).size() == 1);
    if (!shift && (kind == Loop_kind::edge_loop) && is_boundary) {
        const bool same_edge =
            (m_boundary_cycle.mesh.lock()     == pick_result.mesh)     &&
            (m_boundary_cycle.geometry.lock() == pick_result.geometry) &&
            (m_boundary_cycle.edge_key        == edge_key);
        const bool boundary_selected = (same_edge && m_boundary_cycle.boundary_selected)
            ? false
            : selection.is_loop_selected(target, edge_key, Loop_kind::edge_loop, delimit);
        if (boundary_selected) {
            kind = Loop_kind::boundary_loop;
        }
        m_boundary_cycle = Boundary_cycle{
            .mesh              = pick_result.mesh,
            .geometry          = pick_result.geometry,
            .edge_key          = edge_key,
            .boundary_selected = boundary_selected
        };
    } else {
        m_boundary_cycle = Boundary_cycle{};
    }

    Select_action action = Select_action::replace;
    if (shift) {
        action = selection.is_loop_selected(target, edge_key, kind, delimit) ? Select_action::deselect : Select_action::extend;
    }
    const std::size_t count = selection.select_loop(target, edge_key, kind, action, delimit);
    log_selection->trace("Loop select: {} {} from edge ({}, {}) -> {} elements", c_str(kind), c_str(action), edge_key.first, edge_key.second, count);
    return true;
}

void Mesh_component_selection_tool::on_modifiers_changed()
{
    update_loop_preview();
}

void Mesh_component_selection_tool::invalidate_loop_preview()
{
    m_loop_preview.valid = false;
    m_loop_preview_elements.clear();
    update_loop_preview();
}

void Mesh_component_selection_tool::update_loop_preview()
{
    const Mesh_component_mode mode       = m_mesh_component_selection.get_mode();
    const bool                alt_held   = (m_context.input_state != nullptr) && m_context.input_state->alt;
    const bool                ctrl_held  = (m_context.input_state != nullptr) && m_context.input_state->control;
    Scene_view* const         scene_view = get_hover_scene_view();
    if (!is_mesh_component_mode(mode) || !alt_held || (scene_view == nullptr)) {
        m_loop_preview = Loop_preview{};
        m_loop_preview_elements.clear();
        return;
    }
    const Pick_result pick_result = pick(*scene_view);
    if (!pick_result.valid || (pick_result.edge_v0 == pick_result.edge_v1)) {
        m_loop_preview = Loop_preview{};
        m_loop_preview_elements.clear();
        return;
    }
    const Loop_kind kind = (mode == Mesh_component_mode::face)
        ? Loop_kind::face_loop
        : (ctrl_held ? Loop_kind::edge_ring : Loop_kind::edge_loop);
    const erhe::geometry::Edge_loop_delimit delimit  = get_loop_delimit();
    const Mesh_edge_key                     edge_key = make_edge_key(pick_result.edge_v0, pick_result.edge_v1);
    if (
        m_loop_preview.valid                                            &&
        (m_loop_preview.scene_view      == scene_view)                  &&
        (m_loop_preview.mesh.lock()     == pick_result.mesh)            &&
        (m_loop_preview.primitive_index == pick_result.primitive_index) &&
        (m_loop_preview.geometry.lock() == pick_result.geometry)        &&
        (m_loop_preview.edge_key        == edge_key)                    &&
        (m_loop_preview.kind            == kind)                        &&
        (m_loop_preview.delimit         == delimit)
    ) {
        return;
    }
    m_loop_preview = Loop_preview{
        .valid           = false,
        .scene_view      = scene_view,
        .mesh            = pick_result.mesh,
        .primitive_index = pick_result.primitive_index,
        .geometry        = pick_result.geometry,
        .edge_key        = edge_key,
        .kind            = kind,
        .delimit         = delimit
    };
    m_loop_preview.valid = walk_mesh_loop(*pick_result.geometry, edge_key, kind, delimit, m_loop_preview_elements);
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

    // Only scene content is selectable. Non-content meshes (tools, brushes,
    // controllers, rendertargets, id meshes, ...) must not be component-
    // selectable nor show a hover highlight. pick() is the single choke point
    // for try_ready(), on_select() and the hover highlight, so rejecting here
    // suppresses all three.
    if (!is_content_mesh(*mesh)) {
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

    const erhe::scene::Node* node = mesh.get();
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

auto Mesh_component_selection_tool::begin_slide() -> bool
{
    const Mesh_component_mode mode = m_mesh_component_selection.get_mode();
    if (!is_mesh_component_mode(mode) || (m_context.transform_tool == nullptr) || m_loop_cut.active) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    Viewport_scene_view* viewport_scene_view = scene_view->as_viewport_scene_view();
    if (viewport_scene_view == nullptr) {
        return false;
    }
    const Scalar_edit_kind kind = (mode == Mesh_component_mode::vertex)
        ? Scalar_edit_kind::vertex_slide
        : Scalar_edit_kind::edge_slide;
    if (!m_context.transform_tool->begin_scalar_drag(kind, *viewport_scene_view)) {
        return false;
    }
    // Ready for the length of the slide: ranks the click commands above the
    // other press commands of the same buttons.
    m_modal_confirm_click_command.set_ready();
    m_modal_cancel_click_command.set_ready();
    return true;
}

auto Mesh_component_selection_tool::is_slide_active() const -> bool
{
    return (m_context.transform_tool != nullptr) && m_context.transform_tool->is_scalar_drag_active();
}

auto Mesh_component_selection_tool::is_modal_active() const -> bool
{
    return m_loop_cut.active || is_slide_active();
}

auto Mesh_component_selection_tool::run_modal_action(const Component_modal_action action) -> bool
{
    Transform_tool* const transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        return false;
    }
    // The loop cut mode: confirm cuts (and chains into the slide), cancel
    // ends the mode with the mesh untouched; the toggles fall through.
    if (m_loop_cut.active) {
        bool loop_cut_consumed = false;
        if (action == Component_modal_action::confirm) {
            loop_cut_consumed = confirm_loop_cut();
        } else if (action == Component_modal_action::cancel) {
            log_selection->info("Loop cut cancelled");
            end_loop_cut();
            loop_cut_consumed = true;
        }
        if (!is_modal_active()) {
            m_modal_confirm_click_command.set_inactive();
            m_modal_cancel_click_command.set_inactive();
        }
        return loop_cut_consumed;
    }
    bool consumed = false;
    switch (action) {
        case Component_modal_action::confirm: {
            if (transform_tool->is_scalar_drag_active()) {
                transform_tool->confirm_scalar_drag();
                consumed = true;
            }
            break;
        }
        case Component_modal_action::cancel: {
            consumed = transform_tool->cancel_component_edit();
            break;
        }
        case Component_modal_action::toggle_even:    consumed = transform_tool->toggle_scalar_drag_option(Scalar_drag_option::even);    break;
        case Component_modal_action::toggle_flipped: consumed = transform_tool->toggle_scalar_drag_option(Scalar_drag_option::flipped); break;
        case Component_modal_action::toggle_clamp:   consumed = transform_tool->toggle_scalar_drag_option(Scalar_drag_option::clamp);   break;
        default: break;
    }
    if (consumed) {
        log_selection->trace("slide modal action {}", c_str(action));
    }
    if (!transform_tool->is_scalar_drag_active()) {
        m_modal_confirm_click_command.set_inactive();
        m_modal_cancel_click_command.set_inactive();
    }
    return consumed;
}

#pragma region Loop cut
namespace {

[[nodiscard]] auto is_quad(const GEO::Mesh& mesh, const GEO::index_t facet) -> bool
{
    return mesh.facets.nb_vertices(facet) == 4;
}

[[nodiscard]] auto has_quad_facet(const erhe::geometry::Geometry& geometry, const GEO::index_t edge) -> bool
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (const GEO::index_t facet : geometry.get_edge_facets(edge)) {
        if (is_quad(mesh, facet)) {
            return true;
        }
    }
    return false;
}

// A quad holding both edges other than the excluded facets; GEO::NO_INDEX
// when there is none.
[[nodiscard]] auto find_shared_quad(
    const erhe::geometry::Geometry& geometry,
    const GEO::index_t              edge_a,
    const GEO::index_t              edge_b,
    const GEO::index_t              exclude_0,
    const GEO::index_t              exclude_1
) -> GEO::index_t
{
    const GEO::Mesh& mesh = geometry.get_mesh();
    for (const GEO::index_t facet_a : geometry.get_edge_facets(edge_a)) {
        if ((facet_a == exclude_0) || (facet_a == exclude_1) || !is_quad(mesh, facet_a)) {
            continue;
        }
        for (const GEO::index_t facet_b : geometry.get_edge_facets(edge_b)) {
            if (facet_a == facet_b) {
                return facet_a;
            }
        }
    }
    return GEO::NO_INDEX;
}

// In `facet`, the endpoint of the edge (edge_v0, edge_v1) next to `vertex`
// along the facet boundary; GEO::NO_INDEX when neither is.
[[nodiscard]] auto find_paired_vertex(
    const GEO::Mesh&   mesh,
    const GEO::index_t facet,
    const GEO::index_t vertex,
    const GEO::index_t edge_v0,
    const GEO::index_t edge_v1
) -> GEO::index_t
{
    const GEO::index_t corner_count = mesh.facets.nb_vertices(facet);
    for (GEO::index_t local = 0; local < corner_count; ++local) {
        if (mesh.facets.vertex(facet, local) != vertex) {
            continue;
        }
        const GEO::index_t next = mesh.facets.vertex(facet, (local + 1) % corner_count);
        const GEO::index_t prev = mesh.facets.vertex(facet, (local + corner_count - 1) % corner_count);
        if ((next == edge_v0) || (next == edge_v1)) {
            return next;
        }
        if ((prev == edge_v0) || (prev == edge_v1)) {
            return prev;
        }
    }
    return GEO::NO_INDEX;
}

constexpr int   c_loop_cut_min_cuts         = 1;
constexpr int   c_loop_cut_max_cuts         = 500;
constexpr float c_loop_cut_smoothness_step  = 0.05f;
constexpr float c_loop_cut_smoothness_limit = 1.0f;

} // anonymous namespace

auto Mesh_component_selection_tool::compute_loop_cut_ring(
    const erhe::geometry::Geometry& geometry,
    const GEO::index_t              edge,
    std::vector<GEO::index_t>&      out_edges
) const -> erhe::geometry::Walk_shape
{
    if (!has_quad_facet(geometry, edge)) {
        out_edges.clear();
        out_edges.push_back(edge);
        return erhe::geometry::Walk_shape::open;
    }
    return erhe::geometry::walk_edge_ring(geometry, edge, out_edges);
}

void Mesh_component_selection_tool::invalidate_loop_cut_preview()
{
    m_loop_cut_preview.valid = false;
    m_loop_cut_lines.clear();
    m_loop_cut_line_normals.clear();
    m_loop_cut_points.clear();
    update_loop_cut_preview();
}

void Mesh_component_selection_tool::update_loop_cut_preview()
{
    const auto clear_preview = [this]() {
        m_loop_cut_preview = Loop_cut_preview{};
        m_loop_cut_lines.clear();
        m_loop_cut_line_normals.clear();
        m_loop_cut_points.clear();
    };
    Scene_view* const scene_view = get_hover_scene_view();
    if (!m_loop_cut.active || (scene_view == nullptr) || !is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        clear_preview();
        return;
    }
    const Pick_result pick_result = pick(*scene_view);
    if (!pick_result.valid || (pick_result.edge_v0 == pick_result.edge_v1)) {
        clear_preview();
        return;
    }
    const erhe::geometry::Geometry& geometry = *pick_result.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        clear_preview();
        return;
    }
    const Mesh_edge_key edge_key = make_edge_key(pick_result.edge_v0, pick_result.edge_v1);
    if (
        m_loop_cut_preview.valid                                            &&
        (m_loop_cut_preview.scene_view      == scene_view)                  &&
        (m_loop_cut_preview.mesh.lock()     == pick_result.mesh)            &&
        (m_loop_cut_preview.primitive_index == pick_result.primitive_index) &&
        (m_loop_cut_preview.geometry.lock() == pick_result.geometry)        &&
        (m_loop_cut_preview.edge_key        == edge_key)                    &&
        (m_loop_cut_preview.cuts            == m_loop_cut.cuts)
    ) {
        return;
    }
    clear_preview();
    const GEO::index_t edge = geometry.get_edge(edge_key.first, edge_key.second);
    if (edge == GEO::NO_EDGE) {
        return;
    }

    // The cut points: `cuts` points at i / (cuts + 1) along every ring edge.
    const GEO::Mesh&                 mesh  = geometry.get_mesh();
    const erhe::geometry::Walk_shape shape = compute_loop_cut_ring(geometry, edge, m_loop_cut_ring);
    const int                        cuts  = m_loop_cut.cuts;
    const float                      step  = 1.0f / static_cast<float>(cuts + 1);
    const auto position = [&mesh](const GEO::index_t vertex) -> glm::vec3 {
        return to_glm_vec3(get_pointf(mesh.vertices, vertex));
    };
    for (const GEO::index_t ring_edge : m_loop_cut_ring) {
        const glm::vec3 p0 = position(mesh.edges.vertex(ring_edge, 0));
        const glm::vec3 p1 = position(mesh.edges.vertex(ring_edge, 1));
        for (int k = 1; k <= cuts; ++k) {
            m_loop_cut_points.push_back(glm::mix(p0, p1, static_cast<float>(k) * step));
        }
    }

    // The cut segments between consecutive ring edges. The endpoints of the
    // next edge pair with the current edge's through the quad holding both
    // (each endpoint with its neighbour along the quad boundary), so the
    // segments at the same fraction never cross.
    const auto add_segments = [&](const GEO::index_t facet, const GEO::index_t a0, const GEO::index_t a1, const GEO::index_t b0, const GEO::index_t b1) {
        const glm::vec3 normal = to_glm_vec3(mesh_facet_normalf(mesh, facet));
        const glm::vec3 pa0    = position(a0);
        const glm::vec3 pa1    = position(a1);
        const glm::vec3 pb0    = position(b0);
        const glm::vec3 pb1    = position(b1);
        for (int k = 1; k <= cuts; ++k) {
            const float t = static_cast<float>(k) * step;
            m_loop_cut_lines.push_back(erhe::renderer::Line{glm::mix(pa0, pa1, t), glm::mix(pb0, pb1, t)});
            m_loop_cut_line_normals.push_back(normal);
        }
    };
    const std::size_t ring_size = m_loop_cut_ring.size();
    if (ring_size >= 2) {
        GEO::index_t a0             = mesh.edges.vertex(m_loop_cut_ring[0], 0);
        GEO::index_t a1             = mesh.edges.vertex(m_loop_cut_ring[0], 1);
        GEO::index_t first_facet    = GEO::NO_INDEX;
        GEO::index_t previous_facet = GEO::NO_INDEX;
        bool         paired_all     = true;
        for (std::size_t i = 0; (i + 1) < ring_size; ++i) {
            const GEO::index_t next_edge = m_loop_cut_ring[i + 1];
            const GEO::index_t facet     = find_shared_quad(geometry, m_loop_cut_ring[i], next_edge, GEO::NO_INDEX, GEO::NO_INDEX);
            if (facet == GEO::NO_INDEX) {
                paired_all = false;
                break;
            }
            const GEO::index_t n0 = mesh.edges.vertex(next_edge, 0);
            const GEO::index_t n1 = mesh.edges.vertex(next_edge, 1);
            const GEO::index_t b0 = find_paired_vertex(mesh, facet, a0, n0, n1);
            if (b0 == GEO::NO_INDEX) {
                paired_all = false;
                break;
            }
            const GEO::index_t b1 = (b0 == n0) ? n1 : n0;
            add_segments(facet, a0, a1, b0, b1);
            if (i == 0) {
                first_facet = facet;
            }
            previous_facet = facet;
            a0 = b0;
            a1 = b1;
        }
        // A closed ring: the last and the first edge share one more quad.
        if (paired_all && (shape == erhe::geometry::Walk_shape::closed)) {
            const GEO::index_t first_edge = m_loop_cut_ring[0];
            const GEO::index_t facet      = find_shared_quad(geometry, m_loop_cut_ring[ring_size - 1], first_edge, first_facet, previous_facet);
            if (facet != GEO::NO_INDEX) {
                const GEO::index_t n0 = mesh.edges.vertex(first_edge, 0);
                const GEO::index_t n1 = mesh.edges.vertex(first_edge, 1);
                const GEO::index_t b0 = find_paired_vertex(mesh, facet, a0, n0, n1);
                if (b0 != GEO::NO_INDEX) {
                    add_segments(facet, a0, a1, b0, (b0 == n0) ? n1 : n0);
                }
            }
        }
    }

    m_loop_cut_preview = Loop_cut_preview{
        .valid           = true,
        .scene_view      = scene_view,
        .mesh            = pick_result.mesh,
        .primitive_index = pick_result.primitive_index,
        .geometry        = pick_result.geometry,
        .edge_key        = edge_key,
        .cuts            = cuts
    };
}

auto Mesh_component_selection_tool::begin_loop_cut() -> bool
{
    if (m_loop_cut.active) {
        return true;
    }
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode()) || (m_context.transform_tool == nullptr)) {
        return false;
    }
    if (is_slide_active() || m_context.transform_tool->is_component_edit_active()) {
        return false;
    }
    Scene_view* const scene_view = get_hover_scene_view();
    if ((scene_view == nullptr) || (scene_view->as_viewport_scene_view() == nullptr) || !pick(*scene_view).valid) {
        return false;
    }
    m_loop_cut = Loop_cut_state{
        .active     = true,
        .cuts       = c_loop_cut_min_cuts,
        .smoothness = 0.0f,
        .typed_cuts = 0
    };
    // Ready for the length of the mode: the wheel out-ranks the fly-camera
    // zoom, the click commands the other press commands of their buttons.
    m_loop_cut_wheel_command.set_ready();
    m_modal_confirm_click_command.set_ready();
    m_modal_cancel_click_command.set_ready();
    invalidate_loop_cut_preview();
    log_selection->info("Loop cut started");
    return true;
}

void Mesh_component_selection_tool::end_loop_cut()
{
    m_loop_cut.active     = false;
    m_loop_cut.typed_cuts = 0;
    m_loop_cut_preview    = Loop_cut_preview{};
    m_loop_cut_lines.clear();
    m_loop_cut_line_normals.clear();
    m_loop_cut_points.clear();
    m_loop_cut_wheel_command.set_inactive();
}

void Mesh_component_selection_tool::set_loop_cut_cuts(const int cuts)
{
    const int clamped = std::clamp(cuts, c_loop_cut_min_cuts, c_loop_cut_max_cuts);
    if (clamped == m_loop_cut.cuts) {
        return;
    }
    m_loop_cut.cuts = clamped;
    log_selection->trace("Loop cut: {} cuts", clamped);
    invalidate_loop_cut_preview();
}

auto Mesh_component_selection_tool::run_loop_cut_action(const Loop_cut_action action, const int digit) -> bool
{
    if (!m_loop_cut.active) {
        return false;
    }
    switch (action) {
        case Loop_cut_action::more_cuts: {
            m_loop_cut.typed_cuts = 0;
            set_loop_cut_cuts(m_loop_cut.cuts + 1);
            break;
        }
        case Loop_cut_action::fewer_cuts: {
            m_loop_cut.typed_cuts = 0;
            set_loop_cut_cuts(m_loop_cut.cuts - 1);
            break;
        }
        case Loop_cut_action::more_smoothness:
        case Loop_cut_action::less_smoothness: {
            const float delta = (action == Loop_cut_action::more_smoothness) ? c_loop_cut_smoothness_step : -c_loop_cut_smoothness_step;
            m_loop_cut.smoothness = std::clamp(m_loop_cut.smoothness + delta, -c_loop_cut_smoothness_limit, c_loop_cut_smoothness_limit);
            log_selection->trace("Loop cut: smoothness {}", m_loop_cut.smoothness);
            break;
        }
        case Loop_cut_action::type_digit: {
            // Typed digits accumulate into the count; a count past the
            // maximum starts over from the digit.
            int typed = (m_loop_cut.typed_cuts * 10) + digit;
            if (typed > c_loop_cut_max_cuts) {
                typed = digit;
            }
            m_loop_cut.typed_cuts = typed;
            if (typed >= c_loop_cut_min_cuts) {
                set_loop_cut_cuts(typed);
            }
            break;
        }
        case Loop_cut_action::start:
        default: {
            break;
        }
    }
    return true;
}

auto Mesh_component_selection_tool::adjust_loop_cut_wheel(const float wheel_delta, const uint32_t modifier_mask) -> bool
{
    if (!m_loop_cut.active) {
        return false;
    }
    if (wheel_delta == 0.0f) {
        return true;
    }
    const bool alt = (modifier_mask & erhe::window::Key_modifier_bit_menu) != 0;
    const bool up  = (wheel_delta > 0.0f);
    if (alt) {
        return run_loop_cut_action(up ? Loop_cut_action::more_smoothness : Loop_cut_action::less_smoothness, 0);
    }
    return run_loop_cut_action(up ? Loop_cut_action::more_cuts : Loop_cut_action::fewer_cuts, 0);
}

auto Mesh_component_selection_tool::perform_loop_cut(
    const Mesh_component_target& target,
    const Mesh_edge_key          edge_key,
    const int                    cuts,
    const float                  smoothness,
    Loop_cut_step&               out_step,
    std::string&                 error
) -> bool
{
    Mesh_component_selection& selection   = m_mesh_component_selection;
    const Mesh_component_mode mode_before = selection.get_mode();
    if (!is_mesh_component_mode(mode_before)) {
        error = "loop cut needs a vertex, edge or face mode";
        return false;
    }
    const std::shared_ptr<erhe::scene::Mesh>& mesh = target.mesh;
    if (!mesh || !target.geometry || (mesh->get_item_host() == nullptr) || (m_context.mesh_memory == nullptr)) {
        error = "loop cut needs a mesh in a scene";
        return false;
    }
    const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
    if (
        (target.primitive_index >= primitives.size())               ||
        !primitives[target.primitive_index].primitive               ||
        !primitives[target.primitive_index].primitive->render_shape ||
        (primitives[target.primitive_index].primitive->render_shape->get_geometry_const() != target.geometry)
    ) {
        error = "loop cut target is not the mesh primitive's current geometry";
        return false;
    }
    const erhe::scene::Mesh_primitive before_mesh_primitive = primitives[target.primitive_index];
    const erhe::geometry::Geometry&   geometry              = *target.geometry;
    if (!geometry.has_connectivity() || !geometry.has_edge_connectivity()) {
        error = "loop cut needs the geometry's connectivity, which is not built: " + mesh->get_name();
        return false;
    }
    const GEO::index_t edge = geometry.get_edge(edge_key.first, edge_key.second);
    if (edge == GEO::NO_EDGE) {
        error = "(" + std::to_string(edge_key.first) + ", " + std::to_string(edge_key.second) + ") is not an edge of " + mesh->get_name();
        return false;
    }

    // The ring, and the single edge case: a seed without a quad facet cuts
    // itself only and selects nothing.
    const erhe::geometry::Walk_shape shape       = compute_loop_cut_ring(geometry, edge, m_loop_cut_ring);
    const bool                       single_edge = !has_quad_facet(geometry, edge);
    const GEO::Mesh&                 source_mesh = geometry.get_mesh();
    std::set<std::pair<GEO::index_t, GEO::index_t>> ring_edges;
    for (const GEO::index_t ring_edge : m_loop_cut_ring) {
        ring_edges.insert(make_edge_key(source_mesh.edges.vertex(ring_edge, 0), source_mesh.edges.vertex(ring_edge, 1)));
    }

    std::shared_ptr<erhe::geometry::Geometry> after_geometry = std::make_shared<erhe::geometry::Geometry>(geometry.get_name());
    erhe::geometry::operation::Subdivide_edges_result subdivide_result;
    erhe::geometry::operation::subdivide_edges(
        geometry,
        *after_geometry,
        ring_edges,
        erhe::geometry::operation::Subdivide_edges_options{
            .cuts       = std::clamp(cuts, c_loop_cut_min_cuts, c_loop_cut_max_cuts),
            .smoothness = smoothness,
            .only_quads = false
        },
        &subdivide_result
    );
    for (const std::string& warning : after_geometry->sanitize()) {
        log_selection->warn("Loop cut on '{}' sanitized: {}", mesh->get_name(), warning);
    }
    const std::string validation_error = after_geometry->validate();
    if (!validation_error.empty()) {
        error = "loop cut result failed validation: " + validation_error;
        return false;
    }
    after_geometry->process({.flags =
        erhe::geometry::Geometry::process_flag_connect |
        erhe::geometry::Geometry::process_flag_build_edges |
        erhe::geometry::Geometry::process_flag_compute_smooth_vertex_normals |
        erhe::geometry::Geometry::process_flag_generate_facet_texture_coordinates
    });

    const erhe::primitive::Build_info           build_info      = make_rebuild_build_info(*m_context.mesh_memory, *after_geometry);
    std::shared_ptr<erhe::primitive::Primitive> after_primitive = std::make_shared<erhe::primitive::Primitive>(after_geometry);
    const bool renderable_ok = after_primitive->make_renderable_mesh(build_info, before_mesh_primitive.primitive->render_shape->get_normal_style());
    const bool raytrace_ok   = after_primitive->make_raytrace();
    if (!renderable_ok || !raytrace_ok) {
        error = "loop cut: building the result primitive failed";
        return false;
    }
    erhe::scene::Mesh_primitive after_mesh_primitive = before_mesh_primitive;
    after_mesh_primitive.primitive = after_primitive;

    // Swap the cut in place (D3: the topology step of the gesture). The
    // pre-cut selection entry goes dormant with the before geometry.
    {
        erhe::Item_host* const item_host = mesh->get_item_host();
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{item_host->item_host_mutex};
        std::vector<erhe::scene::Mesh_primitive> new_primitives = primitives;
        new_primitives[target.primitive_index] = after_mesh_primitive;
        swap_mesh_primitives(mesh, new_primitives);
    }
    m_context.app_message_bus->mesh_geometry_changed.send_message(Mesh_geometry_changed_message{.mesh = mesh});

    // Edge mode after the swap, so the conversion only touches live entries
    // and the dormant pre-cut entry stays as it was for a cancel or an undo;
    // then the inner edges (the new loops) are the selection.
    selection.set_mode(Mesh_component_mode::edge);
    std::size_t inner_edges = 0;
    if (!single_edge && !subdivide_result.inner_edges.empty()) {
        std::set<GEO::index_t>  vertices;
        std::set<Mesh_edge_key> edges;
        for (const std::pair<GEO::index_t, GEO::index_t>& inner_edge : subdivide_result.inner_edges) {
            edges.insert(make_edge_key(inner_edge.first, inner_edge.second));
            vertices.insert(inner_edge.first);
            vertices.insert(inner_edge.second);
        }
        selection.set_after_operation(mesh, target.primitive_index, after_geometry, vertices, std::set<GEO::index_t>{}, edges);
        inner_edges = edges.size();
    }

    out_step = Loop_cut_step{
        .topology = Scalar_topology_step{
            .mesh            = mesh,
            .primitive_index = target.primitive_index,
            .before          = before_mesh_primitive,
            .after           = after_mesh_primitive,
            .description     = "Loop Cut",
            .mode_before     = mode_before
        },
        .ring_length = m_loop_cut_ring.size(),
        .ring_shape  = shape,
        .inner_edges = inner_edges
    };
    log_selection->info(
        "Loop cut: '{}' ring of {} edges ({}), {} cuts -> {} vertices, {} facets, {} inner edges",
        mesh->get_name(), out_step.ring_length, (shape == erhe::geometry::Walk_shape::closed) ? "closed" : "open",
        cuts, after_geometry->get_mesh().vertices.nb(), after_geometry->get_mesh().facets.nb(), inner_edges
    );
    return true;
}

void Mesh_component_selection_tool::queue_loop_cut(const Scalar_topology_step& topology)
{
    m_context.operation_stack->queue(
        std::make_shared<Fork_geometry_operation>(
            Fork_geometry_operation::Parameters{
                .mesh            = topology.mesh,
                .primitive_index = topology.primitive_index,
                .before          = topology.before,
                .after           = topology.after,
                .description     = topology.description
            }
        )
    );
}

auto Mesh_component_selection_tool::confirm_loop_cut() -> bool
{
    update_loop_cut_preview();
    if (!m_loop_cut_preview.valid) {
        return true; // nothing under the pointer: the mode keeps running
    }
    Scene_view* const scene_view = get_hover_scene_view();
    const std::shared_ptr<erhe::scene::Mesh>        mesh     = m_loop_cut_preview.mesh.lock();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = m_loop_cut_preview.geometry.lock();
    if (
        (scene_view == nullptr) ||
        (scene_view != m_loop_cut_preview.scene_view) ||
        !m_mesh_component_selection.is_live(mesh, m_loop_cut_preview.primitive_index, geometry)
    ) {
        return true;
    }
    Viewport_scene_view* const viewport_scene_view = scene_view->as_viewport_scene_view();
    const Mesh_component_target target{
        .mesh            = mesh,
        .primitive_index = m_loop_cut_preview.primitive_index,
        .geometry        = geometry
    };
    const Mesh_edge_key edge_key   = m_loop_cut_preview.edge_key;
    const int           cuts       = m_loop_cut.cuts;
    const float         smoothness = m_loop_cut.smoothness;
    end_loop_cut();

    Loop_cut_step step;
    std::string   error;
    if (!perform_loop_cut(target, edge_key, cuts, smoothness, step, error)) {
        log_selection->warn("Loop cut refused: {}", error);
        return true;
    }
    // Chain into the edge slide of the new loops, from the pointer position
    // at the click; its confirm / cancel commit / cancel the whole gesture.
    if (
        (step.inner_edges > 0) &&
        (viewport_scene_view != nullptr) &&
        m_context.transform_tool->begin_scalar_drag(Scalar_edit_kind::edge_slide, *viewport_scene_view, &step.topology)
    ) {
        m_modal_confirm_click_command.set_ready();
        m_modal_cancel_click_command.set_ready();
        return true;
    }
    queue_loop_cut(step.topology);
    return true;
}

auto Mesh_component_selection_tool::loop_cut(
    const Mesh_component_target& target,
    const Mesh_edge_key          edge_key,
    const int                    cuts,
    const float                  smoothness,
    const Scalar_input&          slide,
    Loop_cut_result&             result,
    std::string&                 error
) -> bool
{
    Transform_tool* const transform_tool = m_context.transform_tool;
    if (transform_tool == nullptr) {
        error = "Transform tool not available";
        return false;
    }
    if (is_modal_active() || transform_tool->is_component_edit_active()) {
        error = "another loop cut, slide or component edit is active";
        return false;
    }
    Loop_cut_step step;
    if (!perform_loop_cut(target, edge_key, cuts, smoothness, step, error)) {
        return false;
    }
    result = Loop_cut_result{
        .ring_length = step.ring_length,
        .ring_shape  = step.ring_shape,
        .inner_edges = step.inner_edges
    };
    if (step.inner_edges > 0) {
        Scalar_edit_result slide_result{};
        std::string        slide_error;
        if (transform_tool->run_scalar_edit(Scalar_edit_kind::edge_slide, slide, slide_result, slide_error, &step.topology)) {
            result.slide_vertices = slide_result.slide_vertices;
            result.moved_vertices = slide_result.moved_vertices;
            result.loops          = slide_result.loops;
            return true;
        }
        log_selection->warn("Loop cut: the slide was refused ({}); the cut is committed alone", slide_error);
    }
    queue_loop_cut(step.topology);
    return true;
}
#pragma endregion Loop cut

auto Mesh_component_selection_tool::try_ready() const -> bool
{
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        return false;
    }
    // A running slide or loop cut owns the clicks (confirm / cancel).
    if (is_modal_active()) {
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
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
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
    selection.flush();
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
    const Mesh_component_mode mode           = m_mesh_component_selection.get_mode();
    const bool                component_mode = is_mesh_component_mode(mode);
    const bool                external_hover = is_mesh_component_mode(m_external_hover.mode);
    if (!component_mode && !external_hover) {
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
        if (!component_mode) {
            break;
        }
        if (!m_mesh_component_selection.is_live(entry)) {
            continue;
        }
        const std::shared_ptr<erhe::scene::Mesh>        mesh     = entry.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        const erhe::scene::Node*                        node     = mesh.get();

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

        // Crease sharpness overlay (doc/erhe/subdivision_crease_edges.md): every
        // edge carrying an edge_sharpness value is drawn colored by a viridis
        // gradient mapped over the min..max of the present finite values
        // (values are unclamped floats; infinity renders at the top of the
        // range). Drawn before the selected edges so selection stays on top.
        {
            const erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            float crease_min = std::numeric_limits<float>::max();
            float crease_max = std::numeric_limits<float>::lowest();
            bool  any_crease = false;
            for (GEO::index_t edge = 0, end = geo_mesh.edges.nb(); edge < end; ++edge) {
                const std::optional<float> sharpness = attributes.edge_sharpness.try_get(edge);
                if (!sharpness.has_value()) {
                    continue;
                }
                any_crease = true;
                if (!std::isinf(sharpness.value())) {
                    crease_min = std::min(crease_min, sharpness.value());
                    crease_max = std::max(crease_max, sharpness.value());
                }
            }
            if (any_crease) {
                const bool  has_range = crease_max > crease_min;
                line_renderer.set_thickness(style.edge_thickness);
                for (GEO::index_t edge = 0, end = geo_mesh.edges.nb(); edge < end; ++edge) {
                    const std::optional<float> sharpness = attributes.edge_sharpness.try_get(edge);
                    if (!sharpness.has_value()) {
                        continue;
                    }
                    const float t = std::isinf(sharpness.value())
                        ? 1.0f
                        : (has_range ? std::clamp((sharpness.value() - crease_min) / (crease_max - crease_min), 0.0f, 1.0f) : 1.0f);
                    const glm::vec4 color{gradient::viridis.get(t), 1.0f};
                    const GEO::index_t v0 = geo_mesh.edges.vertex(edge, 0);
                    const GEO::index_t v1 = geo_mesh.edges.vertex(edge, 1);
                    const erhe::renderer::Line line{
                        to_glm_vec3(get_pointf(geo_mesh.vertices, v0)),
                        to_glm_vec3(get_pointf(geo_mesh.vertices, v1))
                    };
                    const Edge_surface_frame frame = compute_edge_surface_frame(*geometry, world_from_node, normal_matrix, v0, v1);
                    line_renderer.add_surface_lines(
                        world_from_node, color,
                        std::span<const erhe::renderer::Line>{&line, 1},
                        std::span<const glm::vec3>{&frame.normal_a, 1},
                        std::span<const glm::vec3>{&frame.normal_b, 1},
                        std::span<const float>{&frame.sign_a, 1}
                    );
                }
            }
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
    // The pointer hover wins; otherwise the external hover (a Geometry
    // Spreadsheet row) is drawn, in its own component kind.
    // The loop cut mode draws its own preview instead of the pointer hover.
    Pick_result         hover      = (component_mode && !m_loop_cut.active && (get_hover_scene_view() == &context.scene_view))
        ? pick(context.scene_view)
        : Pick_result{};
    Mesh_component_mode hover_mode = mode;
    if (!hover.valid && external_hover) {
        hover      = resolve_external_hover();
        hover_mode = m_external_hover.mode;
    }
    if (hover.valid) {
        const erhe::scene::Node* hover_node = hover.mesh.get();
        if (hover_node != nullptr) {
            const glm::mat4  world_from_node = hover_node->world_from_node();
            const GEO::Mesh& geo_mesh        = hover.geometry->get_mesh();
            switch (hover_mode) {
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

    // Loop select preview: what an Alt / Ctrl+Alt click would select,
    // computed by update_loop_preview() on change; drawn only in the view it
    // was picked in and only while its target is live.
    if (component_mode && m_loop_preview.valid && (m_loop_preview.scene_view == &context.scene_view)) {
        const std::shared_ptr<erhe::scene::Mesh>        preview_mesh     = m_loop_preview.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> preview_geometry = m_loop_preview.geometry.lock();
        if (m_mesh_component_selection.is_live(preview_mesh, m_loop_preview.primitive_index, preview_geometry)) {
            const glm::mat4  world_from_node = preview_mesh->world_from_node();
            const GEO::Mesh& geo_mesh        = preview_geometry->get_mesh();
            if (m_loop_preview.kind == Loop_kind::face_loop) {
                m_scratch_positions.clear();
                m_scratch_indices.clear();
                for (const GEO::index_t facet : m_loop_preview_elements) {
                    append_facet_triangles(*preview_geometry, facet);
                }
                if (!m_scratch_indices.empty()) {
                    triangle_renderer.add_triangles(world_from_node, style.hover_color, m_scratch_positions, m_scratch_indices);
                }
            } else {
                const glm::mat3 normal_matrix = glm::transpose(glm::inverse(glm::mat3(world_from_node)));
                m_scratch_lines.clear();
                m_scratch_normals.clear();
                for (const GEO::index_t edge : m_loop_preview_elements) {
                    const GEO::index_t v0 = geo_mesh.edges.vertex(edge, 0);
                    const GEO::index_t v1 = geo_mesh.edges.vertex(edge, 1);
                    m_scratch_lines.push_back(
                        erhe::renderer::Line{
                            to_glm_vec3(get_pointf(geo_mesh.vertices, v0)),
                            to_glm_vec3(get_pointf(geo_mesh.vertices, v1))
                        }
                    );
                    m_scratch_normals.push_back(edge_world_normal(*preview_geometry, normal_matrix, v0, v1));
                }
                if (!m_scratch_lines.empty()) {
                    line_renderer.set_thickness(style.edge_thickness - 1.0f);
                    line_renderer.add_lines(
                        world_from_node, style.hover_color,
                        std::span<const erhe::renderer::Line>{m_scratch_lines},
                        std::span<const glm::vec3>{m_scratch_normals}
                    );
                }
            }
        }
    }

    // Loop cut preview: the cut segments and cut points of the ring under the
    // pointer, computed by update_loop_cut_preview() on change; drawn only in
    // the view it was picked in and only while its target is live.
    if (component_mode && m_loop_cut.active && m_loop_cut_preview.valid && (m_loop_cut_preview.scene_view == &context.scene_view)) {
        const std::shared_ptr<erhe::scene::Mesh>        preview_mesh     = m_loop_cut_preview.mesh.lock();
        const std::shared_ptr<erhe::geometry::Geometry> preview_geometry = m_loop_cut_preview.geometry.lock();
        if (m_mesh_component_selection.is_live(preview_mesh, m_loop_cut_preview.primitive_index, preview_geometry)) {
            const glm::mat4 world_from_node = preview_mesh->world_from_node();
            const glm::mat3 normal_matrix   = glm::transpose(glm::inverse(glm::mat3(world_from_node)));
            m_scratch_normals.clear();
            for (const glm::vec3& normal : m_loop_cut_line_normals) {
                const glm::vec3 world  = normal_matrix * normal;
                const float     length = glm::length(world);
                m_scratch_normals.push_back((length > 1e-6f) ? (world / length) : glm::vec3{0.0f});
            }
            if (!m_loop_cut_lines.empty()) {
                line_renderer.set_thickness(style.edge_thickness - 1.0f);
                line_renderer.add_lines(
                    world_from_node, style.hover_color,
                    std::span<const erhe::renderer::Line>{m_loop_cut_lines},
                    std::span<const glm::vec3>{m_scratch_normals}
                );
            }
            m_scratch_positions.clear();
            m_scratch_indices.clear();
            for (const glm::vec3& point_local : m_loop_cut_points) {
                const glm::vec3 point_world = glm::vec3{world_from_node * glm::vec4{point_local, 1.0f}};
                const float     half        = style.vertex_size * glm::distance(camera_position, point_world);
                append_vertex_quad(point_world, camera_right, camera_up, half);
            }
            if (!m_scratch_indices.empty()) {
                triangle_renderer.add_triangles(glm::mat4{1.0f}, style.hover_color, m_scratch_positions, m_scratch_indices);
            }
        }
    }
}

void Mesh_component_selection_tool::set_external_hover(
    const std::shared_ptr<erhe::scene::Mesh>&        mesh,
    const std::shared_ptr<erhe::geometry::Geometry>& geometry,
    const Mesh_component_mode                        mode,
    const GEO::index_t                               element,
    const GEO::index_t                               edge_v0,
    const GEO::index_t                               edge_v1
)
{
    m_external_hover = External_hover{
        .mesh     = mesh,
        .geometry = geometry,
        .mode     = mode,
        .element  = element,
        .edge_v0  = edge_v0,
        .edge_v1  = edge_v1
    };
}

auto Mesh_component_selection_tool::get_hovered_content_position() const -> std::optional<glm::vec3>
{
    Scene_view* const scene_view = get_last_hover_scene_view();
    if (scene_view == nullptr) {
        return std::nullopt;
    }
    const Hover_entry& content = scene_view->get_hover(Hover_entry::content_slot);
    if (!content.valid || !content.position.has_value()) {
        return std::nullopt;
    }
    return content.position.value();
}

void Mesh_component_selection_tool::clear_external_hover()
{
    m_external_hover = External_hover{};
}

auto Mesh_component_selection_tool::resolve_external_hover() const -> Pick_result
{
    Pick_result result{};
    const std::shared_ptr<erhe::scene::Mesh>        mesh     = m_external_hover.mesh.lock();
    const std::shared_ptr<erhe::geometry::Geometry> geometry = m_external_hover.geometry.lock();
    if (!mesh || !geometry || (mesh->get_item_host() == nullptr)) {
        return result;
    }
    const GEO::Mesh& geo_mesh = geometry->get_mesh();
    switch (m_external_hover.mode) {
        case Mesh_component_mode::vertex: {
            if (m_external_hover.element >= geo_mesh.vertices.nb()) {
                return result;
            }
            result.vertex = m_external_hover.element;
            break;
        }
        case Mesh_component_mode::face: {
            if (m_external_hover.element >= geo_mesh.facets.nb()) {
                return result;
            }
            result.facet = m_external_hover.element;
            break;
        }
        case Mesh_component_mode::edge: {
            if ((m_external_hover.edge_v0 >= geo_mesh.vertices.nb()) || (m_external_hover.edge_v1 >= geo_mesh.vertices.nb())) {
                return result;
            }
            result.edge_v0 = m_external_hover.edge_v0;
            result.edge_v1 = m_external_hover.edge_v1;
            break;
        }
        default: {
            return result;
        }
    }
    result.valid    = true;
    result.mesh     = mesh;
    result.geometry = geometry;
    return result;
}

void Mesh_component_selection_tool::viewport_toolbar()
{
    ImGui::PushID("Mesh_component_selection_tool::viewport_toolbar");

    Mesh_component_selection& selection = m_mesh_component_selection;

    // Mode combo (Object / Vertex / Edge / Face / Bone). In a menu bar successive
    // widgets flow horizontally, so the Clear button lands to the right.
    int               mode_index = static_cast<int>(selection.get_mode());
    const char* const items[]    = {"Object", "Vertex", "Edge", "Face", "Bone"};
    if (erhe::imgui::combo_fit_width("##mesh_component_mode", &mode_index, items, IM_ARRAYSIZE(items))) {
        // Ctrl held while choosing the mode expands the selection instead of
        // flushing it (Blender's Ctrl+click on a mode button).
        const Mode_conversion conversion = ImGui::GetIO().KeyCtrl ? Mode_conversion::expand : Mode_conversion::flush;
        selection.set_mode(static_cast<Mesh_component_mode>(mode_index), conversion);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Mesh Component Selection Mode (hold Ctrl while choosing to expand the selection)");
    }

    // Gesture sub-mode (Click / Box / Paint). Box and Paint select the
    // components in a screen region: the id-buffer scan in Face mode, the CPU
    // projection in Vertex / Edge mode. The B / C hotkeys switch to Box / Paint as a shortcut for this combo.
    int               gesture_index   = static_cast<int>(m_gesture_mode);
    const char* const gesture_items[] = {"Click", "Box", "Paint"};
    if (erhe::imgui::combo_fit_width("##mesh_component_gesture", &gesture_index, gesture_items, IM_ARRAYSIZE(gesture_items))) {
        m_gesture_mode = static_cast<Component_gesture_mode>(gesture_index);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Selection gesture: Click picks one component; Box drags a rectangle; Paint drags a brush (B / C hotkeys)");
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
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Shared: edit shared geometry (all instances change). Fork: copy geometry on edit (only this instance changes).");
        }

        // Transform mode: how the gizmo transforms the selected components. Move
        // translates/rotates/scales them in place; Extrude duplicates the selection
        // boundary, bridges it with new faces, then moves the duplicates along the gizmo
        // delta; Extrude (Group Normal) does the same topology change but slides each
        // disjoint subset along its own average normal; Extrude (Vertex Normal) slides each
        // vertex along its own normal; Edge Slide / Vertex Slide map the gizmo translation to
        // the slide factor. The item order matches the Mesh_transform_mode enum values
        // (move, extrude, extrude_group_normal, extrude_vertex_normal, edge_slide, vertex_slide).
        int               transform_index   = static_cast<int>(m_context.editor_settings->transform_mode);
        const char* const transform_items[] = {"Move", "Extrude", "Extrude (Group Normal)", "Extrude (Vertex Normal)", "Edge Slide", "Vertex Slide"};
        if (erhe::imgui::combo_fit_width("##mesh_transform_mode", &transform_index, transform_items, IM_ARRAYSIZE(transform_items))) {
            m_context.editor_settings->transform_mode = static_cast<Mesh_transform_mode>(transform_index);
            m_context.app_settings->settings_store().touch();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Move: drag moves the selected components. Extrude: drag extrudes them (new faces) then moves. Extrude (Group Normal): extrudes, then each disjoint subset slides along its own average normal by the drag amount. Extrude (Vertex Normal): extrudes, then each vertex slides along its own normal. Edge Slide: the selected edge loops slide along their rails by the drag along the nearest vertex's rail. Vertex Slide: each selected vertex slides toward the neighbour the drag points at. G starts a slide from the pointer in any transform mode (Enter / click confirms, Escape / right click cancels, E even, F flipped, C or Alt unclamped).");
        }
    }

    if (ImGui::Button("Clear")) {
        // Abandon any in-flight gesture scan first; otherwise a just-released
        // paint/box stroke's final scan would re-populate the selection a few
        // frames after this clear.
        cancel_pending_scans();
        selection.clear_all();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Clear mesh component selection");
    }

    if (is_mesh_component_mode(selection.get_mode())) {
        class Selection_button
        {
        public:
            const char*                label;
            Component_selection_action action;
            const char*                tooltip;
        };
        static constexpr Selection_button selection_buttons[] = {
            {"All",    Component_selection_action::select_all,                   "Select all (Ctrl+A)"},
            {"None",   Component_selection_action::select_none,                  "Select none (Alt+A)"},
            {"Invert", Component_selection_action::invert,                       "Invert the selection in the current mode (Ctrl+I)"},
            {"Linked", Component_selection_action::select_linked_from_selection, "Select linked from the selection (Ctrl+L; L selects linked under the cursor)"}
        };
        for (const Selection_button& button : selection_buttons) {
            if (ImGui::Button(button.label)) {
                static_cast<void>(run_selection_action(button.action));
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", button.tooltip);
            }
        }
    }

    // Edge loop crease delimit (Alt+click loop select in vertex and edge mode).
    if ((selection.get_mode() == Mesh_component_mode::vertex) || (selection.get_mode() == Mesh_component_mode::edge)) {
        if (ImGui::Checkbox("Loop stops at creases", &m_loop_delimit_crease)) {
            invalidate_loop_preview();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Alt+click edge loop select: a crease edge continues only onto crease edges, a plain edge stops at a vertex with a crease edge");
        }
    }

    // Crease sharpness painting (edge mode): apply / clear the semi-sharp
    // crease sharpness of the selected edges (doc/erhe/subdivision_crease_edges.md).
    // Undoable via Set_edge_sharpness_operation; the value only affects the
    // crease overlay and future Catmull-Clark subdivisions, so no rebuild.
    if (selection.get_mode() == Mesh_component_mode::edge) {
        ImGui::SetNextItemWidth(110.0f);
        ImGui::BeginDisabled(m_crease_infinite);
        ImGui::DragFloat("##crease_sharpness", &m_crease_sharpness, 0.05f, 0.0f, 100.0f, "Crease %.2f");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Crease sharpness: number of Catmull-Clark levels subdivided with the sharp rules (fractional part blends)");
        }
        ImGui::Checkbox("Inf##crease_infinite", &m_crease_infinite);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Infinitely sharp crease");
        }
        if (ImGui::Button("Set Crease")) {
            const float value = m_crease_infinite ? std::numeric_limits<float>::infinity() : m_crease_sharpness;
            apply_crease_sharpness(value);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Set crease sharpness on the selected edges");
        }
        if (ImGui::Button("Clear Crease")) {
            apply_crease_sharpness(std::optional<float>{});
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Remove crease sharpness from the selected edges");
        }

        // Crease value range over the live entries' geometries; matches the
        // min..max mapping the viridis edge overlay uses (infinite values are
        // counted separately and render at the top of the range).
        std::size_t crease_count   = 0;
        std::size_t infinite_count = 0;
        float       crease_min     = std::numeric_limits<float>::max();
        float       crease_max     = std::numeric_limits<float>::lowest();
        for (const Mesh_component_entry& entry : selection.get_entries()) {
            if (!selection.is_live(entry)) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
            if (!geometry) {
                continue;
            }
            const erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            for (GEO::index_t edge = 0, end = geometry->get_mesh().edges.nb(); edge < end; ++edge) {
                const std::optional<float> sharpness = attributes.edge_sharpness.try_get(edge);
                if (!sharpness.has_value()) {
                    continue;
                }
                ++crease_count;
                if (std::isinf(sharpness.value())) {
                    ++infinite_count;
                } else {
                    crease_min = std::min(crease_min, sharpness.value());
                    crease_max = std::max(crease_max, sharpness.value());
                }
            }
        }
        if (crease_count > 0) {
            if (crease_count > infinite_count) {
                ImGui::Text("Crease %.2f .. %.2f (%zu edges, %zu inf)", crease_min, crease_max, crease_count, infinite_count);
            } else {
                ImGui::Text("Crease (%zu inf edges)", infinite_count);
            }
        }
    }

    ImGui::PopID();

    // Visual style (colors, edge thickness, vertex size, edge depth bias) is
    // edited in the Settings window; it is stored editor-global in
    // Editor_settings_config::mesh_component_style so codegen serialization /
    // autosave cover it.
}

void Mesh_component_selection_tool::apply_crease_sharpness(const std::optional<float>& value)
{
    Mesh_component_selection& selection = m_mesh_component_selection;

    std::vector<std::shared_ptr<Operation>> operations;
    for (Mesh_component_entry& entry : selection.get_entries()) {
        if (!selection.is_live(entry) || entry.edges.empty()) {
            continue;
        }
        std::shared_ptr<erhe::geometry::Geometry> geometry = entry.geometry.lock();
        if (!geometry) {
            continue;
        }
        Set_edge_sharpness_operation::Parameters parameters{};
        parameters.geometry = geometry;
        parameters.after    = value;
        const erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
        for (const Mesh_edge_key& key : entry.edges) {
            const GEO::index_t edge = geometry->get_edge(key.first, key.second);
            if (edge == GEO::NO_EDGE) {
                continue;
            }
            parameters.edges .push_back(key);
            parameters.before.push_back(attributes.edge_sharpness.try_get(edge));
        }
        if (parameters.edges.empty()) {
            continue;
        }
        operations.push_back(std::make_shared<Set_edge_sharpness_operation>(std::move(parameters)));
    }

    if (operations.empty()) {
        return;
    }
    if (operations.size() == 1) {
        m_context.operation_stack->queue(operations.front());
    } else {
        m_context.operation_stack->queue(
            std::make_shared<Compound_operation>(
                Compound_operation::Parameters{.operations = std::move(operations)}
            )
        );
    }
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
        if (!is_content_mesh(*mesh)) {
            continue; // only scene content is component-selectable
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
        // Non-blocking: a null geometry is skipped below, so do not build one.
        const std::shared_ptr<erhe::geometry::Geometry> geometry = shape->get_geometry_const();
        if (!geometry) {
            continue;
        }
        // The GPU id pass emits the GEO facet index per vertex, so hit.facet_id is
        // the facet directly (same index space as geometry's GEO mesh facets).
        const GEO::index_t facet_count = static_cast<GEO::index_t>(geometry->get_mesh().facets.nb());
        if (hit.facet_id >= facet_count) {
            continue; // out of range (identity-only mesh writes facet id 0 with no geometry)
        }
        const GEO::index_t facet = static_cast<GEO::index_t>(hit.facet_id);
        Mesh_component_entry& entry = selection.find_or_create_entry(mesh, hit.primitive_index, geometry);
        if (subtract) {
            entry.facets.erase(facet);
        } else {
            entry.add_facet(facet);
        }
    }
    selection.flush();
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
    // Only act (and consume the key) while in a mesh component mode; otherwise
    // the key falls through to other bindings (e.g. brush preview on C).
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        return false;
    }
    m_gesture_mode = mode;
    return true;
}

auto Mesh_component_selection_tool::grow_selection() -> bool
{
    // Only act (and consume the key) while a component mode is active; in Object
    // mode the key falls through to other bindings.
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        return false;
    }
    m_mesh_component_selection.grow();
    return true;
}

auto Mesh_component_selection_tool::shrink_selection() -> bool
{
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        return false;
    }
    m_mesh_component_selection.shrink();
    return true;
}

void Mesh_component_selection_tool::collect_select_all_targets(std::vector<Mesh_component_target>& out_targets)
{
    out_targets.clear();
    const auto append_mesh = [&out_targets](const std::shared_ptr<erhe::scene::Mesh>& mesh) {
        const std::size_t first = out_targets.size();
        append_mesh_component_targets(mesh, out_targets);
        // Drop the new targets already present (a mesh reached both as a
        // live entry and through the object Selection). The targets of one
        // mesh differ in primitive index, so only [0, first) is searched.
        std::size_t write = first;
        for (std::size_t read = first; read < out_targets.size(); ++read) {
            bool duplicate = false;
            for (std::size_t i = 0; i < first; ++i) {
                if (
                    (out_targets[i].mesh            == out_targets[read].mesh)            &&
                    (out_targets[i].primitive_index == out_targets[read].primitive_index) &&
                    (out_targets[i].geometry        == out_targets[read].geometry)
                ) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                if (write != read) {
                    out_targets[write] = std::move(out_targets[read]);
                }
                ++write;
            }
        }
        out_targets.resize(write);
    };

    for (const Mesh_component_entry& entry : m_mesh_component_selection.get_entries()) {
        if (!m_mesh_component_selection.is_live(entry)) {
            continue;
        }
        append_mesh(entry.mesh.lock());
    }
    if (m_context.selection != nullptr) {
        for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
            append_mesh(erhe::scene::get_mesh(item));
        }
    }
    if (out_targets.empty()) {
        Scene_view* const scene_view = get_hover_scene_view();
        if (scene_view != nullptr) {
            const Pick_result pick_result = pick(*scene_view);
            if (pick_result.valid) {
                append_mesh(pick_result.mesh);
            }
        }
    }
}

auto Mesh_component_selection_tool::run_selection_action(const Component_selection_action action) -> bool
{
    Mesh_component_selection& selection = m_mesh_component_selection;
    if (!is_mesh_component_mode(selection.get_mode())) {
        return false;
    }
    switch (action) {
        case Component_selection_action::select_all: {
            collect_select_all_targets(m_select_all_targets);
            selection.select_all(m_select_all_targets);
            m_select_all_targets.clear();
            return true;
        }
        case Component_selection_action::select_none: {
            // As the Clear button: an in-flight gesture scan must not
            // re-populate the selection after this.
            cancel_pending_scans();
            selection.select_none();
            return true;
        }
        case Component_selection_action::invert: {
            selection.invert();
            return true;
        }
        case Component_selection_action::select_linked_under_cursor: {
            Scene_view* const scene_view = get_hover_scene_view();
            if (scene_view == nullptr) {
                return false;
            }
            const Pick_result pick_result = pick(*scene_view);
            if (!pick_result.valid) {
                return false;
            }
            const GEO::Mesh& geo_mesh = pick_result.geometry->get_mesh();
            m_linked_seed_vertices.clear();
            for (GEO::index_t i = 0, corner_count = geo_mesh.facets.nb_corners(pick_result.facet); i < corner_count; ++i) {
                m_linked_seed_vertices.push_back(geo_mesh.facet_corners.vertex(geo_mesh.facets.corner(pick_result.facet, i)));
            }
            static_cast<void>(
                selection.select_linked(
                    Mesh_component_target{
                        .mesh            = pick_result.mesh,
                        .primitive_index = pick_result.primitive_index,
                        .geometry        = pick_result.geometry
                    },
                    m_linked_seed_vertices
                )
            );
            return true;
        }
        case Component_selection_action::select_linked_from_selection: {
            selection.select_linked_from_selection();
            return true;
        }
        default: {
            return false;
        }
    }
}

auto Mesh_component_selection_tool::box_select_try_ready() const -> bool
{
    if ((m_gesture_mode != Component_gesture_mode::box) || is_modal_active()) {
        return false;
    }
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
        return false;
    }
    Scene_view* scene_view = get_hover_scene_view();
    if (scene_view == nullptr) {
        return false;
    }
    // Desktop viewport only (the id-buffer scan and the CPU projection need a
    // Viewport_scene_view).
    return scene_view->as_viewport_scene_view() != nullptr;
}

auto Mesh_component_selection_tool::is_gesture_box_mode() const -> bool
{
    return (m_gesture_mode == Component_gesture_mode::box) &&
           is_mesh_component_mode(m_mesh_component_selection.get_mode());
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
    // Face mode keeps the id-buffer scan warm while dragging; Vertex / Edge
    // mode projects once, at the commit after release.
    if (m_box_active && (m_mesh_component_selection.get_mode() == Mesh_component_mode::face)) {
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
    if ((m_gesture_mode != Component_gesture_mode::paint) || is_modal_active()) {
        return false;
    }
    if (!is_mesh_component_mode(m_mesh_component_selection.get_mode())) {
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
           is_mesh_component_mode(m_mesh_component_selection.get_mode());
}

void Mesh_component_selection_tool::paint_select_update(const glm::vec2 window_position, const bool real_motion)
{
    if (real_motion) {
        if (!m_paint_active) {
            // Stroke just started: lock to the starting view, capture modifiers,
            // and (for a plain stroke) clear the selection once before adding.
            // Only the face path drains async scan results after release.
            m_paint_active               = true;
            m_paint_pending              = (m_mesh_component_selection.get_mode() == Mesh_component_mode::face);
            m_paint_scene_view           = get_hover_scene_view();
            m_paint_last_applied_frame   = 0;
            m_paint_commit_request_frame = 0;
            // Gate frame for this stroke: ignore any scan result that predates it
            // (leftovers from a previous stroke's drain) so the new stroke does
            // not start with the previous stroke's last brush position.
            m_paint_stroke_start_frame   = (m_context.graphics_device != nullptr)
                ? m_context.graphics_device->get_frame_index()
                : 0;
            const bool shift = (m_context.input_state != nullptr) && m_context.input_state->shift;
            m_paint_subtract = (m_context.input_state != nullptr) && m_context.input_state->control;
            if (!m_paint_subtract && !shift) {
                m_mesh_component_selection.clear_all();
            }
        }
        m_brush_center_window = window_position;
    }
    if (!m_paint_active) {
        return;
    }
    if (m_mesh_component_selection.get_mode() == Mesh_component_mode::face) {
        request_paint_scan();
        return;
    }
    // Vertex / Edge mode: project and apply the brush now, once per gesture
    // frame while the button is held.
    const Viewport_scene_view* viewport_scene_view = (m_paint_scene_view != nullptr)
        ? m_paint_scene_view->as_viewport_scene_view()
        : nullptr;
    if (viewport_scene_view == nullptr) {
        return;
    }
    select_components_in_region(
        *viewport_scene_view,
        make_brush_region(*viewport_scene_view),
        m_paint_subtract ? Region_select_operation::subtract : Region_select_operation::add
    );
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
}

void Mesh_component_selection_tool::paint_select_release()
{
    if (!m_paint_active) {
        return;
    }
    m_paint_active = false; // m_paint_pending stays set so gesture_update drains the last results
}

void Mesh_component_selection_tool::cancel_pending_scans()
{
    // Drop any in-flight async region-scan drains so their (already-rendered)
    // results are not applied on a later frame. Only the pending/target
    // bookkeeping is reset; the *_active gesture lifecycle is owned by the input
    // commands and left untouched (a genuinely active drag is not interrupted by
    // a toolbar button press).
    m_box_commit_pending         = false;
    m_box_commit_request_frame   = 0;
    m_paint_pending              = false;
    m_paint_commit_request_frame = 0;
    m_debug_pending              = false;
    m_debug_request_frame        = 0;
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
    // The pointer slide follows the pointer (a no-op unless a slide runs and
    // the pointer or an option changed).
    if (m_context.transform_tool != nullptr) {
        m_context.transform_tool->update_scalar_drag();
    }
    const bool have_devices = (m_context.id_renderer != nullptr) && (m_context.graphics_device != nullptr);

    // Debug/test (MCP debug_region_select): drive a region scan over an explicit
    // viewport rectangle / disk and commit when it completes. Exclusive with the
    // mouse gestures below (only one is ever pending at a time).
    if (m_debug_pending && (m_mesh_component_selection.get_mode() != Mesh_component_mode::face)) {
        // Vertex / Edge mode: the CPU projection, committed on this frame.
        m_debug_pending = false;
        const Viewport_scene_view* viewport_scene_view = get_debug_scene_view();
        if (viewport_scene_view == nullptr) {
            log_selection->warn("debug_region_select: no viewport to project in");
        } else {
            Component_region region{};
            if (m_debug_is_brush) {
                region.shape  = Region_shape::disk;
                region.center = glm::vec2{
                    static_cast<float>(m_debug_x) + (0.5f * static_cast<float>(m_debug_w)),
                    static_cast<float>(m_debug_y) + (0.5f * static_cast<float>(m_debug_h))
                };
                region.radius = m_debug_brush_radius;
            } else {
                region.shape = Region_shape::rectangle;
                region.min   = glm::vec2{static_cast<float>(m_debug_x), static_cast<float>(m_debug_y)};
                region.max   = glm::vec2{static_cast<float>(m_debug_x + m_debug_w), static_cast<float>(m_debug_y + m_debug_h)};
            }
            if (m_debug_replace) {
                m_mesh_component_selection.clear_all();
            }
            select_components_in_region(
                *viewport_scene_view,
                region,
                m_debug_subtract ? Region_select_operation::subtract : Region_select_operation::add
            );
        }
    }
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

    // Box: deferred single commit after release. Face mode waits for a scan
    // whose pixels are from at-or-after the first post-release request; Vertex /
    // Edge mode projects once, now.
    if (m_box_commit_pending) {
        const Mesh_component_mode mode = m_mesh_component_selection.get_mode();
        if ((m_gesture_mode != Component_gesture_mode::box) || !is_mesh_component_mode(mode)) {
            m_box_commit_pending = false;
        } else if (mode != Mesh_component_mode::face) {
            m_box_commit_pending = false;
            const Viewport_scene_view* viewport_scene_view = (m_box_scene_view != nullptr)
                ? m_box_scene_view->as_viewport_scene_view()
                : nullptr;
            if (viewport_scene_view != nullptr) {
                const bool replace = !m_box_modifier_shift && !m_box_modifier_ctrl;
                if (replace) {
                    m_mesh_component_selection.clear_all();
                }
                select_components_in_region(
                    *viewport_scene_view,
                    make_box_region(*viewport_scene_view),
                    m_box_modifier_ctrl ? Region_select_operation::subtract : Region_select_operation::add
                );
            }
        } else if (!have_devices) {
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
        // The drain applies facet hits: drop it when the mode left Face.
        if (!have_devices || (m_mesh_component_selection.get_mode() != Mesh_component_mode::face)) {
            m_paint_pending = false;
        } else {
            // After release, re-request the final brush so its faces are not
            // missed if the release-frame scan was dropped (all slots busy), and
            // freeze the drain target to the first post-release frame. The target
            // must be captured once and not advance: because we re-request the
            // scan every drain frame, a target that tracked the latest request
            // would forever outrun the readback-lagged applied frame, so the
            // drain would never end and the last dab would be re-applied every
            // frame -- which, among other things, undid the Clear button. Mirrors
            // the box path's m_box_commit_request_frame.
            if (!m_paint_active) {
                if (m_paint_commit_request_frame == 0) {
                    m_paint_commit_request_frame = m_context.graphics_device->get_frame_index();
                }
                request_paint_scan();
            }
            // Apply only results from this stroke (frame_number >= the stroke's
            // gate); a result older than the gate is a leftover from a previous
            // stroke's drain and must not be applied to this one.
            const Id_renderer::Scan_result& result = m_context.id_renderer->take_scan_result();
            if (
                result.ready &&
                (result.frame_number >= m_paint_stroke_start_frame) &&
                (result.frame_number >  m_paint_last_applied_frame)
            ) {
                apply_scan_hits_to_selection(m_mesh_component_selection, result, m_paint_subtract);
                m_paint_last_applied_frame = result.frame_number;
            }
            if (
                !m_paint_active &&
                (m_paint_commit_request_frame != 0) &&
                (m_paint_last_applied_frame >= m_paint_commit_request_frame)
            ) {
                m_paint_pending              = false;
                m_paint_commit_request_frame = 0;
            }
        }
    }

    // Keep the brush-radius wheel command Ready while paint-selecting + hovering
    // a viewport, so it out-ranks the fly-camera zoom for the wheel (priority is
    // state-dominated, and sort_mouse_wheel_bindings orders by priority). It is
    // set Inactive otherwise so the wheel zooms the camera as usual.
    Scene_view* const hover_scene_view = get_hover_scene_view();
    const bool paint_wheel_active =
        !m_loop_cut.active &&
        (m_gesture_mode == Component_gesture_mode::paint) &&
        is_mesh_component_mode(m_mesh_component_selection.get_mode()) &&
        (hover_scene_view != nullptr) &&
        (hover_scene_view->as_viewport_scene_view() != nullptr);
    if (paint_wheel_active) {
        m_brush_radius_command.set_ready();
    } else {
        m_brush_radius_command.set_inactive();
    }
    // The loop cut wheel (cut count / smoothness) out-ranks the fly-camera
    // zoom the same way while the loop cut mode runs.
    if (m_loop_cut.active) {
        m_loop_cut_wheel_command.set_ready();
    } else {
        m_loop_cut_wheel_command.set_inactive();
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

auto Mesh_component_selection_tool::Component_region::contains(const glm::vec2 position_in_viewport) const -> bool
{
    switch (shape) {
        case Region_shape::rectangle: {
            return
                (position_in_viewport.x >= min.x) && (position_in_viewport.x <= max.x) &&
                (position_in_viewport.y >= min.y) && (position_in_viewport.y <= max.y);
        }
        case Region_shape::disk: {
            return glm::distance2(position_in_viewport, center) <= (radius * radius);
        }
        default: {
            return false;
        }
    }
}

auto Mesh_component_selection_tool::make_box_region(const Viewport_scene_view& viewport_scene_view) const -> Component_region
{
    const glm::vec2 a = viewport_scene_view.get_viewport_from_window(m_box_anchor_window);
    const glm::vec2 b = viewport_scene_view.get_viewport_from_window(m_box_current_window);
    Component_region region{};
    region.shape = Region_shape::rectangle;
    region.min   = glm::min(a, b);
    region.max   = glm::max(a, b);
    return region;
}

auto Mesh_component_selection_tool::make_brush_region(const Viewport_scene_view& viewport_scene_view) const -> Component_region
{
    Component_region region{};
    region.shape  = Region_shape::disk;
    region.center = viewport_scene_view.get_viewport_from_window(m_brush_center_window);
    region.radius = m_brush_radius;
    return region;
}

auto Mesh_component_selection_tool::get_debug_scene_view() const -> const Viewport_scene_view*
{
    // The view the pointer was last over, as the interactive gestures use;
    // with no hover yet (a fresh headless run), the first viewport window.
    const Scene_view* const last_hover_scene_view = get_last_hover_scene_view();
    if (last_hover_scene_view != nullptr) {
        const Viewport_scene_view* const viewport_scene_view = last_hover_scene_view->as_viewport_scene_view();
        if (viewport_scene_view != nullptr) {
            return viewport_scene_view;
        }
    }
    if (m_context.scene_views == nullptr) {
        return nullptr;
    }
    for (const std::shared_ptr<Viewport_window>& viewport_window : m_context.scene_views->get_viewport_windows()) {
        const std::shared_ptr<Viewport_scene_view> viewport_scene_view = viewport_window->viewport_scene_view();
        if (viewport_scene_view) {
            return viewport_scene_view.get();
        }
    }
    return nullptr;
}

void Mesh_component_selection_tool::select_components_in_region(
    const Viewport_scene_view&    viewport_scene_view,
    const Component_region&       region,
    const Region_select_operation operation
)
{
    Mesh_component_selection& selection = m_mesh_component_selection;
    const Mesh_component_mode mode = selection.get_mode();
    if ((mode != Mesh_component_mode::vertex) && (mode != Mesh_component_mode::edge)) {
        return;
    }
    const std::shared_ptr<Scene_root>          scene_root = viewport_scene_view.get_scene_root();
    const std::shared_ptr<erhe::scene::Camera> camera     = viewport_scene_view.get_camera();
    if (!scene_root || !camera) {
        return;
    }

    // Candidates: the visible content meshes of the view's scene that component
    // selection can address (the same filters as select all).
    m_region_targets.clear();
    {
        const std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};
        const erhe::scene::Mesh_layer* const content_layer = scene_root->layers().content();
        if (content_layer != nullptr) {
            for (const std::shared_ptr<erhe::scene::Mesh>& mesh : content_layer->meshes) {
                if (mesh && mesh->is_visible()) {
                    static_cast<void>(append_mesh_component_targets(mesh, m_region_targets));
                }
            }
        }
    }

    // The projection of Viewport_scene_view::project_to_viewport, with the
    // camera transforms computed once instead of once per vertex.
    const erhe::math::Viewport&                     projection_viewport = viewport_scene_view.get_projection_viewport();
    const erhe::math::Coordinate_conventions        conventions         = viewport_scene_view.get_conventions();
    const erhe::scene::Camera_projection_transforms transforms          = camera->projection_transforms(
        projection_viewport,
        viewport_scene_view.get_reverse_depth(),
        viewport_scene_view.get_depth_range(),
        conventions
    );
    const glm::mat4 clip_from_world = transforms.clip_from_world.get_matrix();

    for (const Mesh_component_target& target : m_region_targets) {
        const glm::mat4    clip_from_node = clip_from_world * target.mesh->world_from_node();
        const GEO::Mesh&   geo_mesh       = target.geometry->get_mesh();
        const GEO::index_t vertex_count   = geo_mesh.vertices.nb();
        m_region_vertex_inside.assign(vertex_count, std::uint8_t{0});
        bool any_inside = false;
        for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
            const glm::vec3 position_in_node = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
            const glm::vec4 clip             = clip_from_node * glm::vec4{position_in_node, 1.0f};
            if (!(clip.w > 0.0f)) {
                continue; // behind the camera
            }
            const glm::vec3 position_in_viewport = erhe::math::project_to_screen_space<float>(
                clip_from_node,
                position_in_node,
                0.0f,
                1.0f,
                static_cast<float>(projection_viewport.x),
                static_cast<float>(projection_viewport.y),
                static_cast<float>(projection_viewport.width),
                static_cast<float>(projection_viewport.height),
                conventions
            );
            if (region.contains(glm::vec2{position_in_viewport})) {
                m_region_vertex_inside[vertex] = 1;
                any_inside = true;
            }
        }
        if (!any_inside) {
            continue;
        }

        Mesh_component_entry* const entry = (operation == Region_select_operation::subtract)
            ? selection.find_entry(target.mesh, target.primitive_index, target.geometry)
            : &selection.find_or_create_entry(target.mesh, target.primitive_index, target.geometry);
        if (entry == nullptr) {
            continue; // nothing selected on this target to subtract from
        }

        if (mode == Mesh_component_mode::vertex) {
            for (GEO::index_t vertex = 0; vertex < vertex_count; ++vertex) {
                if (m_region_vertex_inside[vertex] == 0) {
                    continue;
                }
                if (operation == Region_select_operation::subtract) {
                    static_cast<void>(entry->vertices.erase(vertex));
                } else {
                    entry->add_vertex(vertex);
                }
            }
        } else {
            // An edge is inside when both of its endpoints are.
            for (GEO::index_t facet = 0, facet_count = geo_mesh.facets.nb(); facet < facet_count; ++facet) {
                const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
                for (GEO::index_t i = 0; i < corner_count; ++i) {
                    const GEO::index_t v0 = geo_mesh.facet_corners.vertex(geo_mesh.facets.corner(facet, i));
                    const GEO::index_t v1 = geo_mesh.facet_corners.vertex(geo_mesh.facets.corner(facet, (i + 1) % corner_count));
                    if ((m_region_vertex_inside[v0] == 0) || (m_region_vertex_inside[v1] == 0)) {
                        continue;
                    }
                    if (operation == Region_select_operation::subtract) {
                        static_cast<void>(entry->edges.erase(make_edge_key(v0, v1)));
                    } else {
                        entry->add_edge(v0, v1);
                    }
                }
            }
        }
    }
    // Drop the targets now: the scratch must not keep meshes alive across
    // frames (a closed scene or an undone insert must release them).
    m_region_targets.clear();
    selection.flush();
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
        draw_list->AddRect      (p0, p1, line_color, 0.0f, 1.5f, ImDrawFlags_None);
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
