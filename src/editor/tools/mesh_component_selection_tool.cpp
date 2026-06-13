#include "tools/mesh_component_selection_tool.hpp"

#include "tools/mesh_component_selection.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "input_state.hpp"
#include "renderers/render_context.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_renderer/primitive_renderer.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_window/window_event_handler.hpp"

#include <geogram/mesh/mesh.h>

#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>

#include <imgui/imgui.h>

#include <limits>

using erhe::geometry::get_pointf;
using erhe::geometry::to_glm_vec3;
using erhe::geometry::mesh_facet_normalf;

namespace editor {

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
#pragma endregion Commands

Mesh_component_selection_tool::Mesh_component_selection_tool(
    erhe::commands::Commands&    commands,
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    App_context&                 context,
    App_message_bus&             app_message_bus,
    Mesh_component_selection&    mesh_component_selection,
    Tools&                       tools
)
    : Tool                      {context, tools, Tool_flags::background}
    , m_mesh_component_selection{mesh_component_selection}
    , m_window                  {imgui_renderer, imgui_windows, "Mesh Components", "mesh_components", [this]() { window_imgui(); }}
    , m_select_command          {commands, context}
{
    set_base_priority(c_priority);
    set_description  ("Mesh Component Selection");

    m_select_command.set_host(this);
    commands.register_command            (&m_select_command);
    commands.bind_command_to_mouse_button(&m_select_command, erhe::window::Mouse_button_left, false);

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
    // Switching to a different mesh (or to a rebuilt geometry) clears the
    // previous component selection.
    selection.set_active_mesh(pick_result.mesh, pick_result.primitive_index);
    selection.set_active_geometry(pick_result.geometry);

    // Plain click replaces (clear then add); Ctrl/Shift extend (toggle).
    const bool extend = (m_context.input_state != nullptr) &&
                        (m_context.input_state->control || m_context.input_state->shift);

    switch (selection.get_mode()) {
        case Mesh_component_mode::vertex: {
            if (extend) {
                selection.toggle_vertex(pick_result.vertex);
            } else {
                selection.clear();
                selection.add_vertex(pick_result.vertex);
            }
            break;
        }
        case Mesh_component_mode::edge: {
            if (extend) {
                selection.toggle_edge(pick_result.edge_v0, pick_result.edge_v1);
            } else {
                selection.clear();
                selection.add_edge(pick_result.edge_v0, pick_result.edge_v1);
            }
            break;
        }
        case Mesh_component_mode::face: {
            if (extend) {
                selection.toggle_facet(pick_result.facet);
            } else {
                selection.clear();
                selection.add_facet(pick_result.facet);
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
    erhe::renderer::Primitive_renderer line_renderer     = context.get({erhe::graphics::Primitive_type::line,     2, true, true });

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

    // Persisted selection of the active mesh.
    const std::shared_ptr<erhe::scene::Mesh> active_mesh     = m_mesh_component_selection.get_active_mesh();
    const std::size_t                        primitive_index = m_mesh_component_selection.get_active_primitive_index();
    if (active_mesh && (primitive_index < active_mesh->get_primitives().size())) {
        const erhe::scene::Node*                           node      = active_mesh->get_node();
        const std::shared_ptr<erhe::primitive::Primitive>& primitive = active_mesh->get_primitives()[primitive_index].primitive;
        if ((node != nullptr) && primitive) {
            const std::shared_ptr<erhe::primitive::Primitive_shape> shape = primitive->get_shape_for_raytrace();
            if (shape) {
                const std::shared_ptr<erhe::geometry::Geometry>& geometry = shape->get_geometry();
                if (geometry) {
                    // Drop the selection if the mesh's geometry has been
                    // replaced (a geometry edit / undo) since it was picked;
                    // the stored indices would otherwise address the new mesh.
                    m_mesh_component_selection.set_active_geometry(geometry);

                    const glm::mat4  world_from_node = node->world_from_node();
                    const glm::mat3  normal_matrix   = glm::transpose(glm::inverse(glm::mat3(world_from_node)));
                    const GEO::Mesh& geo_mesh        = geometry->get_mesh();

                    // Faces.
                    m_scratch_positions.clear();
                    m_scratch_indices.clear();
                    for (const GEO::index_t facet : m_mesh_component_selection.get_facets()) {
                        append_facet_triangles(*geometry, facet);
                    }
                    if (!m_scratch_indices.empty()) {
                        triangle_renderer.add_triangles(world_from_node, m_face_color, m_scratch_positions, m_scratch_indices);
                    }

                    // Edges. Each carries a world-space surface normal so the
                    // line shader pushes it off the surface (NdotV^2 bias).
                    m_scratch_lines.clear();
                    m_scratch_normals.clear();
                    for (const Mesh_edge_key& edge : m_mesh_component_selection.get_edges()) {
                        const glm::vec3 p0 = to_glm_vec3(get_pointf(geo_mesh.vertices, edge.first));
                        const glm::vec3 p1 = to_glm_vec3(get_pointf(geo_mesh.vertices, edge.second));
                        m_scratch_lines.push_back(erhe::renderer::Line{p0, p1});
                        m_scratch_normals.push_back(edge_world_normal(*geometry, normal_matrix, edge.first, edge.second));
                    }
                    if (!m_scratch_lines.empty()) {
                        line_renderer.set_thickness(m_edge_thickness);
                        line_renderer.add_lines(
                            world_from_node, m_edge_color,
                            std::span<const erhe::renderer::Line>{m_scratch_lines},
                            std::span<const glm::vec3>{m_scratch_normals}
                        );
                    }

                    // Vertices (camera-facing quads, world space).
                    m_scratch_positions.clear();
                    m_scratch_indices.clear();
                    for (const GEO::index_t vertex : m_mesh_component_selection.get_vertices()) {
                        const glm::vec3 v_local = to_glm_vec3(get_pointf(geo_mesh.vertices, vertex));
                        const glm::vec3 v_world = glm::vec3{world_from_node * glm::vec4{v_local, 1.0f}};
                        const float     half    = m_vertex_handle_size * glm::distance(camera_position, v_world);
                        append_vertex_quad(v_world, camera_right, camera_up, half);
                    }
                    if (!m_scratch_indices.empty()) {
                        triangle_renderer.add_triangles(glm::mat4{1.0f}, m_vertex_color, m_scratch_positions, m_scratch_indices);
                    }
                }
            }
        }
    }

    // Hover highlight for the component under the pointer in this view.
    const Pick_result hover = pick(context.scene_view);
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
                        triangle_renderer.add_triangles(world_from_node, m_hover_color, m_scratch_positions, m_scratch_indices);
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
                    line_renderer.set_thickness(m_edge_thickness + 1.0f);
                    line_renderer.add_lines(
                        world_from_node, m_hover_color,
                        std::span<const erhe::renderer::Line>{m_scratch_lines},
                        std::span<const glm::vec3>{m_scratch_normals}
                    );
                    break;
                }
                case Mesh_component_mode::vertex: {
                    const glm::vec3 v_local = to_glm_vec3(get_pointf(geo_mesh.vertices, hover.vertex));
                    const glm::vec3 v_world = glm::vec3{world_from_node * glm::vec4{v_local, 1.0f}};
                    const float     half    = (m_vertex_handle_size * 1.3f) * glm::distance(camera_position, v_world);
                    m_scratch_positions.clear();
                    m_scratch_indices.clear();
                    append_vertex_quad(v_world, camera_right, camera_up, half);
                    triangle_renderer.add_triangles(glm::mat4{1.0f}, m_hover_color, m_scratch_positions, m_scratch_indices);
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

void Mesh_component_selection_tool::window_imgui()
{
    Mesh_component_selection& selection = m_mesh_component_selection;

    int                mode_index = static_cast<int>(selection.get_mode());
    const char* const  items[]    = {"Object", "Vertex", "Edge", "Face"};
    if (ImGui::Combo("Mode", &mode_index, items, IM_ARRAYSIZE(items))) {
        selection.set_mode(static_cast<Mesh_component_mode>(mode_index));
    }

    ImGui::Text("Vertices: %d", static_cast<int>(selection.get_vertices().size()));
    ImGui::Text("Edges: %d",    static_cast<int>(selection.get_edges().size()));
    ImGui::Text("Faces: %d",    static_cast<int>(selection.get_facets().size()));
    if (ImGui::Button("Clear")) {
        selection.clear();
    }

    ImGui::ColorEdit4("Vertex Color", &m_vertex_color.x, ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit4("Edge Color",   &m_edge_color.x,   ImGuiColorEditFlags_NoInputs);
    ImGui::ColorEdit4("Face Color",   &m_face_color.x,   ImGuiColorEditFlags_NoInputs);
    ImGui::DragFloat ("Edge Thickness", &m_edge_thickness,      0.1f,   0.5f, 20.0f);
    ImGui::DragFloat ("Vertex Size",    &m_vertex_handle_size,  0.001f, 0.001f, 0.1f);
}

} // namespace editor
