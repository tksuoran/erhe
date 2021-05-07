#include "tools/trs_tool.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"
#include "log.hpp"
#include "scene/scene_manager.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/text_renderer.hpp"
#include "operations/operation_stack.hpp"
#include "operations/insert_operation.hpp"

#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe_tracy.hpp"

#include "imgui.h"

namespace editor
{

using namespace std;
using namespace glm;
using namespace erhe::scene;
using namespace erhe::primitive;
using namespace erhe::toolkit;
using namespace erhe::geometry;
using namespace erhe::geometry::shapes;

auto Trs_tool::state() const -> Tool::State
{
    return m_state;
}

void Trs_tool::set_translate(bool enabled)
{
    m_visualization.show_translate = enabled;
    update_visibility();
}

void Trs_tool::set_rotate(bool enabled)
{
    m_visualization.show_rotate = enabled;
    update_visibility();
}

void Trs_tool::set_node(std::shared_ptr<Node> node)
{
    if (node == m_target_node)
    {
        return;
    }

    // Attach new host to manipulator
    m_target_node = node;
    if (m_target_node)
    {
        m_visualization.root = m_target_node.get();
        m_visualization.tool_node.parent = m_visualization.root;
    }
    else
    {
        m_visualization.root = nullptr;
        m_visualization.tool_node.parent = nullptr;
    }

    update_visibility();
}

void Trs_tool::Visualization::update_scale(vec3 view_position_in_world)
{
    ZoneScoped;

    if (root == nullptr)
    {
        return;
    }
    vec3 position_in_world = root->position_in_world();
    float distance = length(position_in_world - vec3(view_position_in_world));
    mat4 parent_from_node = erhe::toolkit::create_scale(scale * distance / 100.0f);
    //root->transforms.parent_from_node.set_rotation(pi<float>() / 4.0f, vec3(0.0f, 1.0f, 0.0f));
    tool_node.transforms.parent_from_node.set(parent_from_node);
    update_transforms();
}

void Trs_tool::Visualization::update_visibility(bool visible, Handle active_handle)
{
    ZoneScoped;

    constexpr const uint64_t visible_mask = erhe::scene::Mesh::c_visibility_tool | erhe::scene::Mesh::c_visibility_id;
    bool show_all = visible && (active_handle == Handle::e_handle_none);
    y_arrow_cylinder_mesh->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    z_arrow_cylinder_mesh->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    xy_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xy) || show_all) ? visible_mask : 0;
    xz_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xz) || show_all) ? visible_mask : 0;
    yz_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_yz) || show_all) ? visible_mask : 0;
    x_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_x    ) || show_all) ? visible_mask : 0;
    y_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_y    ) || show_all) ? visible_mask : 0;
    z_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_z    ) || show_all) ? visible_mask : 0;
    x_arrow_cylinder_mesh->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_x ) || show_all) ? visible_mask : 0;
    x_arrow_cone_mesh    ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_x ) || show_all) ? visible_mask : 0;
    y_arrow_cylinder_mesh->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    y_arrow_cone_mesh    ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    z_arrow_cylinder_mesh->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    z_arrow_cone_mesh    ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    xy_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xy) || show_all) ? visible_mask : 0;
    xz_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xz) || show_all) ? visible_mask : 0;
    yz_box_mesh          ->visibility_mask = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_yz) || show_all) ? visible_mask : 0;
    x_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_x    ) || show_all) ? visible_mask : 0;
    y_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_y    ) || show_all) ? visible_mask : 0;
    z_rotate_ring_mesh   ->visibility_mask = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_z    ) || show_all) ? visible_mask : 0;

    x_arrow_cylinder_mesh->primitives.front().material = (active_handle == Handle::e_handle_translate_x ) ? highlight_material : x_material;
    x_arrow_cone_mesh    ->primitives.front().material = (active_handle == Handle::e_handle_translate_x ) ? highlight_material : x_material;
    y_arrow_cylinder_mesh->primitives.front().material = (active_handle == Handle::e_handle_translate_y ) ? highlight_material : y_material;
    y_arrow_cone_mesh    ->primitives.front().material = (active_handle == Handle::e_handle_translate_y ) ? highlight_material : y_material;
    z_arrow_cylinder_mesh->primitives.front().material = (active_handle == Handle::e_handle_translate_z ) ? highlight_material : z_material;
    z_arrow_cone_mesh    ->primitives.front().material = (active_handle == Handle::e_handle_translate_z ) ? highlight_material : z_material;
    xy_box_mesh          ->primitives.front().material = (active_handle == Handle::e_handle_translate_xy) ? highlight_material : z_material;
    xz_box_mesh          ->primitives.front().material = (active_handle == Handle::e_handle_translate_xz) ? highlight_material : y_material;
    yz_box_mesh          ->primitives.front().material = (active_handle == Handle::e_handle_translate_yz) ? highlight_material : x_material;
    x_rotate_ring_mesh   ->primitives.front().material = (active_handle == Handle::e_handle_rotate_x    ) ? highlight_material : x_material;
    y_rotate_ring_mesh   ->primitives.front().material = (active_handle == Handle::e_handle_rotate_y    ) ? highlight_material : y_material;
    z_rotate_ring_mesh   ->primitives.front().material = (active_handle == Handle::e_handle_rotate_z    ) ? highlight_material : z_material;
}

Trs_tool::Visualization::Visualization(Scene_manager* scene_manager)
{
    if (scene_manager == nullptr)
    {
        return;
    }
    x_material         = make_shared<Material>("x",         vec4(1.00f, 0.00f, 0.0f, 1.0f));
    y_material         = make_shared<Material>("y",         vec4(0.23f, 1.00f, 0.0f, 1.0f));
    z_material         = make_shared<Material>("z",         vec4(0.00f, 0.23f, 1.0f, 1.0f));
    highlight_material = make_shared<Material>("highlight", vec4(1.00f, 0.70f, 0.1f, 1.0f));

    scene_manager->add(x_material);
    scene_manager->add(y_material);
    scene_manager->add(z_material);
    scene_manager->add(highlight_material);

    float arrow_cylinder_length    = 2.5f;
    float arrow_cone_length        = 1.0f;
    float arrow_cylinder_radius    = 0.08f;
    float arrow_cone_radius        = 0.35f;
    float box_half_thickness       = 0.02f;
    float box_length               = 1.0f;
    float arrow_tip                = arrow_cylinder_length + arrow_cone_length;
    float rotate_ring_major_radius = 4.0f;
    float rotate_ring_minor_radius = 0.25f;
    auto  arrow_cylinder_geometry  = make_cylinder(0.0, arrow_cylinder_length, arrow_cylinder_radius, true, true, 32, 4);
    auto  arrow_cone_geometry      = make_cone    (arrow_cylinder_length, arrow_tip, arrow_cone_radius, true, 32, 4);
    auto  box_geometry             = make_box     (0.0, box_length, 0.0, box_length, -box_half_thickness, box_half_thickness);
    auto  rotate_ring_geometry     = make_torus   (rotate_ring_major_radius, rotate_ring_minor_radius, 80, 32);
    // Torus geometry is on xz plane, swap x and y to make it yz plane
    rotate_ring_geometry.transform(mat4_swap_xy);
    rotate_ring_geometry.reverse_polygons();
    auto arrow_cylinder_pg = scene_manager->make_primitive_geometry(std::move(arrow_cylinder_geometry));
    auto arrow_cone_pg     = scene_manager->make_primitive_geometry(std::move(arrow_cone_geometry    ));
    auto box_pg            = scene_manager->make_primitive_geometry(std::move(box_geometry           ));
    auto rotate_ring_pg    = scene_manager->make_primitive_geometry(std::move(rotate_ring_geometry   ));
    auto tool_layer        = scene_manager->tool_layer();
    vec3 pos{0.0f, 1.0f, 0.0f};
    x_arrow_cylinder_mesh  = scene_manager->make_mesh_node("X arrow cylinder", arrow_cylinder_pg, x_material, tool_layer, &tool_node, pos);
    x_arrow_cone_mesh      = scene_manager->make_mesh_node("X arrow cone",     arrow_cone_pg,     x_material, tool_layer, &tool_node, pos);
    y_arrow_cylinder_mesh  = scene_manager->make_mesh_node("Y arrow cylinder", arrow_cylinder_pg, y_material, tool_layer, &tool_node, pos);
    y_arrow_cone_mesh      = scene_manager->make_mesh_node("Y arrow cone",     arrow_cone_pg,     y_material, tool_layer, &tool_node, pos);
    z_arrow_cylinder_mesh  = scene_manager->make_mesh_node("Z arrow cylinder", arrow_cylinder_pg, z_material, tool_layer, &tool_node, pos);
    z_arrow_cone_mesh      = scene_manager->make_mesh_node("Z arrow cone",     arrow_cone_pg,     z_material, tool_layer, &tool_node, pos);
    xy_box_mesh            = scene_manager->make_mesh_node("XY box",           box_pg,            z_material, tool_layer, &tool_node, pos);
    xz_box_mesh            = scene_manager->make_mesh_node("XZ box",           box_pg,            y_material, tool_layer, &tool_node, pos);
    yz_box_mesh            = scene_manager->make_mesh_node("YZ box",           box_pg,            x_material, tool_layer, &tool_node, pos);
    x_rotate_ring_mesh     = scene_manager->make_mesh_node("X rotate ring",    rotate_ring_pg,    x_material, tool_layer, &tool_node, pos);
    y_rotate_ring_mesh     = scene_manager->make_mesh_node("Y rotate ring",    rotate_ring_pg,    y_material, tool_layer, &tool_node, pos);
    z_rotate_ring_mesh     = scene_manager->make_mesh_node("Z rotate ring",    rotate_ring_pg,    z_material, tool_layer, &tool_node, pos);

    x_arrow_cylinder_mesh->node->transforms.parent_from_node.set         (mat4(1));
    x_arrow_cone_mesh    ->node->transforms.parent_from_node.set         (mat4(1));
    y_arrow_cylinder_mesh->node->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(0.0f, 0.0f, 1.0f));
    y_arrow_cone_mesh    ->node->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(0.0f, 0.0f, 1.0f));
    z_arrow_cylinder_mesh->node->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));
    z_arrow_cone_mesh    ->node->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));
    xy_box_mesh          ->node->transforms.parent_from_node.set         (mat4(1));
    xz_box_mesh          ->node->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(1.0f, 0.0f, 0.0f));
    yz_box_mesh          ->node->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));

    x_rotate_ring_mesh->node->transforms.parent_from_node.set         (mat4(1));
    y_rotate_ring_mesh->node->transforms.parent_from_node.set_rotation( pi<float>() / 2.0f, vec3(0.0f, 0.0f, 1.0f));
    z_rotate_ring_mesh->node->transforms.parent_from_node.set_rotation(-pi<float>() / 2.0f, vec3(0.0f, 1.0f, 0.0f));

    //root->transforms.parent_from_node.set_rotation(pi<float>() / 4.0f, vec3(0.0f, 1.0f, 0.0f));

    update_visibility(false, Handle::e_handle_none);
    update_transforms();
}

Trs_tool::Trs_tool(const std::shared_ptr<Operation_stack>& operation_stack,
                   const std::shared_ptr<Scene_manager>&   scene_manager,
                   const std::shared_ptr<Selection_tool>&  selection_tool)
    : m_operation_stack{operation_stack}
    , m_scene_manager{scene_manager}
    , m_visualization{scene_manager.get()}
{
    ZoneScoped;

    m_handles[m_visualization.x_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_visualization.x_arrow_cone_mesh    .get()] = Handle::e_handle_translate_x;
    m_handles[m_visualization.y_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_visualization.y_arrow_cone_mesh    .get()] = Handle::e_handle_translate_y;
    m_handles[m_visualization.z_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_visualization.z_arrow_cone_mesh    .get()] = Handle::e_handle_translate_z;
    m_handles[m_visualization.xy_box_mesh          .get()] = Handle::e_handle_translate_xy;
    m_handles[m_visualization.xz_box_mesh          .get()] = Handle::e_handle_translate_xz;
    m_handles[m_visualization.yz_box_mesh          .get()] = Handle::e_handle_translate_yz;
    m_handles[m_visualization.x_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_x;
    m_handles[m_visualization.y_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_y;
    m_handles[m_visualization.z_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_z;

    if (selection_tool)
    {
        auto lambda = [this](const Selection_tool::Mesh_collection& meshes)
        {
            bool node_found{false};
            for (auto mesh : meshes)
            {
                if (mesh)
                {
                    auto node = mesh->node;
                    if (node)
                    {
                        node_found = true;
                        set_node(node);
                        break;
                    }
                }
            }
            if (!node_found)
            {
                set_node({});
            }
        };
        m_selection_subscription = selection_tool->subscribe_mesh_selection_change_notification(lambda);
    }
}

void Trs_tool::window(Pointer_context&)
{
    ZoneScoped;

    bool show_translate = m_visualization.show_translate;
    bool show_rotate    = m_visualization.show_rotate;
    ImGui::Begin      ("Transform`");
    ImGui::SliderFloat("Scale",                 &m_visualization.scale, 1.0f, 10.0f);
    ImGui::Checkbox   ("Translate Tool",        &m_visualization.show_translate);
    ImGui::Checkbox   ("Translate Snap Enable", &m_translate_snap_enable);
    const float translate_snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
    const char* translate_snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
    static int  translate_item_current = 0;
    ImGui::Combo("Translate Snap", &translate_item_current, translate_snap_items, IM_ARRAYSIZE(translate_snap_items));
    m_translate_snap = translate_snap_values[translate_item_current];
    ImGui::Separator  ();
    ImGui::Checkbox   ("Rotate Tool",        &m_visualization.show_rotate);
    ImGui::Checkbox   ("Rotate Snap Enable", &m_rotate_snap_enable);
    //const float rotate_snap_values[] = {  5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
    const char* rotate_snap_items [] = { "5",  "10",  "15",  "20",  "30",  "45",  "60",  "90" };
    static int  rotate_item_current = 0;
    ImGui::Combo("Rotate Snap", &rotate_item_current, rotate_snap_items, IM_ARRAYSIZE(rotate_snap_items));
    ImGui::Separator  ();
    ImGui::Checkbox   ("Hide Inactive", &m_visualization.hide_inactive);
    ImGui::Separator  ();
    ImGui::End();
    if ((show_translate != m_visualization.show_translate) ||
        (show_rotate    != m_visualization.show_rotate   ))
    {
        update_visibility();
    }
}

auto Trs_tool::update(Pointer_context& pointer_context) -> bool
{
    ZoneScoped;

    if ((m_state == State::passive) && pointer_context.mouse_button[Mouse_button_left].pressed) 
    {
        return begin(pointer_context);
    }
    if ((m_state != State::passive) && pointer_context.mouse_button[Mouse_button_left].released)
    {
        return end(pointer_context);
    }
    if ((m_state == State::ready) && pointer_context.mouse_moved)
    {
        log_tools.trace("Trs tool state = Active\n");
        m_state = State::active;
    }
    if (m_state != State::active)
    {
        // We might be ready, but not consuming event yet
        return false;
    }

    auto handle_type = get_handle_type(m_active_handle);
    switch (handle_type)
    {
        case Handle_type::e_handle_type_translate_axis:  update_axis_translate (pointer_context); return true;
        case Handle_type::e_handle_type_translate_plane: update_plane_translate(pointer_context); return true;
        case Handle_type::e_handle_type_rotate:          update_rotate         (pointer_context); return true;
        case Handle_type::e_handle_type_none:
        default:
            return false;
            break;
    }
}

auto Trs_tool::get_axis_direction() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_translate_x:  return m_drag.initial_world_from_local[0];
        case Handle::e_handle_translate_y:  return m_drag.initial_world_from_local[1];
        case Handle::e_handle_translate_z:  return m_drag.initial_world_from_local[2];
        case Handle::e_handle_translate_yz: return m_drag.initial_world_from_local[0];
        case Handle::e_handle_translate_xz: return m_drag.initial_world_from_local[1];
        case Handle::e_handle_translate_xy: return m_drag.initial_world_from_local[2];
        case Handle::e_handle_rotate_x:     return m_drag.initial_world_from_local[0];
        case Handle::e_handle_rotate_y:     return m_drag.initial_world_from_local[1];
        case Handle::e_handle_rotate_z:     return m_drag.initial_world_from_local[2];
        default:
            FATAL("bad axis\n");
            break;
    }
}

void Trs_tool::update_axis_translate(Pointer_context& pointer_context)
{
    ZoneScoped;

    vec3 drag_world_direction = get_axis_direction();
    vec3 P0    = m_drag.initial_position_in_world - drag_world_direction;
    vec3 P1    = m_drag.initial_position_in_world + drag_world_direction;
    vec3 Q0    = pointer_context.position_in_world(1.0f);
    vec3 Q1    = pointer_context.position_in_world(0.0f);
    vec3 ss_P0 = pointer_context.position_in_window(P0);
    vec3 ss_P1 = pointer_context.position_in_window(P1);
    vec2 ss_closest;
    bool use_ss = closest_point(vec2(ss_P0),
                                vec2(ss_P1),
                                vec2(static_cast<float>(pointer_context.pointer_x),
                                     static_cast<float>(pointer_context.pointer_y)),
                                ss_closest);
    vec3 R0 = pointer_context.position_in_world(vec3(ss_closest, 0.0f));
    vec3 R1 = pointer_context.position_in_world(vec3(ss_closest, 1.0f));
    vec3 PC_q;
    vec3 QC;
    vec3 PC_r;
    vec3 RC;

    bool has_closest_points_q = closest_points(P0, P1, Q0, Q1, PC_q, QC);
    bool has_closest_points_r = closest_points(P0, P1, R0, R1, PC_r, RC);
    if (use_ss && !has_closest_points_r)
    {
        use_ss = false;
        if (!has_closest_points_q)
        {
            return;
        }
    }

    vec3 drag_point_new_position_in_world = use_ss ? PC_r : PC_q;
    vec3 translation_vector = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    snap_translate(translation_vector);
    mat4 translation = create_translation(translation_vector);

    m_visualization.root->transforms.parent_from_node.set(translation * m_drag.initial_world_from_local);
    update_transforms();
}

auto Trs_tool::is_x_translate_active() const -> bool
{
    return (m_active_handle == Handle::e_handle_translate_x ) ||
           (m_active_handle == Handle::e_handle_translate_xy) ||
           (m_active_handle == Handle::e_handle_translate_xz);
}

auto Trs_tool::is_y_translate_active() const -> bool
{
    return (m_active_handle == Handle::e_handle_translate_y ) ||
           (m_active_handle == Handle::e_handle_translate_xy) ||
           (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_z_translate_active() const -> bool
{
    return (m_active_handle == Handle::e_handle_translate_z ) ||
           (m_active_handle == Handle::e_handle_translate_xz) ||
           (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_rotate_active() const -> bool
{
    return (m_active_handle == Handle::e_handle_rotate_x) ||
           (m_active_handle == Handle::e_handle_rotate_y) ||
           (m_active_handle == Handle::e_handle_rotate_z);
}

void Trs_tool::snap_translate(glm::vec3& translation) const
{
    if (!m_translate_snap_enable)
    {
        return;
    }
    if (is_x_translate_active())
    {
        translation.x = std::floor((translation.x + m_translate_snap * 0.5f) / m_translate_snap) * m_translate_snap;
    }
    if (is_y_translate_active())
    {
        translation.y = std::floor((translation.y + m_translate_snap * 0.5f) / m_translate_snap) * m_translate_snap;
    }
    if (is_z_translate_active())
    {
        translation.z = std::floor((translation.z + m_translate_snap * 0.5f) / m_translate_snap) * m_translate_snap;
    }
}

auto Trs_tool::get_plane_normal() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_translate_xy: return m_drag.initial_world_from_local[2];
        case Handle::e_handle_translate_xz: return m_drag.initial_world_from_local[1];
        case Handle::e_handle_translate_yz: return m_drag.initial_world_from_local[0];
        case Handle::e_handle_rotate_x:     return m_drag.initial_world_from_local[0];
        case Handle::e_handle_rotate_y:     return m_drag.initial_world_from_local[1];
        case Handle::e_handle_rotate_z:     return m_drag.initial_world_from_local[2];
        default:
            FATAL("bad handle for plane");
            break;
    }
}

auto Trs_tool::get_plane_normal_in_model() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_rotate_x: return vec3(1.0f, 0.0f, 0.0f);
        case Handle::e_handle_rotate_y: return vec3(0.0f, 1.0f, 0.0f);
        case Handle::e_handle_rotate_z: return vec3(0.0f, 0.0f, 1.0f);
        default:
            // TODO plane translation?
            FATAL("bad handle for plane");
            break;
    }
}

auto Trs_tool::get_plane_side_in_model() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_rotate_x: return vec3( 0.0f, 0.0f, 1.0f);
        case Handle::e_handle_rotate_y: return vec3( 0.0f, 0.0f, 1.0f);
        case Handle::e_handle_rotate_z: return vec3(-1.0f, 0.0f, 0.0f);
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::get_plane_side_in_model2() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_rotate_x: return vec3( 0.0f, 1.0f, 0.0f);
        case Handle::e_handle_rotate_y: return vec3( 1.0f, 0.0f, 0.0f);
        case Handle::e_handle_rotate_z: return vec3( 0.0f, 1.0f, 0.0f);
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

void Trs_tool::update_plane_translate(Pointer_context& pointer_context)
{
    ZoneScoped;

    vec3 p0 = m_drag.initial_position_in_world;
    vec3 n  = get_plane_normal();

    vec3 Q0 = pointer_context.position_in_world(1.0f);
    vec3 Q1 = pointer_context.position_in_world(0.0f);

    vec3 v = normalize(Q1 - Q0);

    float t = 0.0f;
    bool intersection_exists = intersect_plane(n, p0, Q0, v, t);
    if (!intersection_exists)
    {
        return;
    }

    vec3 drag_point_new_position_in_world = Q0 + t * v;
    vec3 translation_vector = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    snap_translate(translation_vector);
    mat4 translation = create_translation(translation_vector);

    m_visualization.root->transforms.parent_from_node.set(translation * m_drag.initial_world_from_local);
    update_transforms();
}

auto Trs_tool::offset_plane_origo(Handle handle, vec3 p) const -> vec3
{
    switch (handle)
    {
        case Handle::e_handle_rotate_x: return vec3( p.x, 0.0f, 0.0f);
        case Handle::e_handle_rotate_y: return vec3(0.0f,  p.y, 0.0f);
        case Handle::e_handle_rotate_z: return vec3(0.0f, 0.0f,  p.z);
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::project_to_offset_plane(Handle handle, vec3 p, vec3 q) const -> vec3
{
    switch (handle)
    {
        case Handle::e_handle_rotate_x: return vec3(p.x, q.y, q.z);
        case Handle::e_handle_rotate_y: return vec3(q.x, p.y, q.z);
        case Handle::e_handle_rotate_z: return vec3(q.x, q.y, p.z);
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::project_pointer_to_plane(Pointer_context& pointer_context, vec3 p, vec3& q) -> bool
{
    vec3  n  = get_plane_normal_in_model();
    vec3  q0 = vec3(m_drag.initial_local_from_world * vec4(pointer_context.position_in_world(0.0f), 1.0f));
    vec3  q1 = vec3(m_drag.initial_local_from_world * vec4(pointer_context.position_in_world(1.0f), 1.0f));
    vec3  v  = normalize(q1 - q0);
    float t  = 0.0f;
    bool  ok = intersect_plane(n, p, q0, v, t);
    if (!ok)
    {
        return false;
    }

    q = q0 + t * v;
    return true;
 }

void Trs_tool::update_rotate(Pointer_context& pointer_context)
{
    ZoneScoped;

    // r = local drag start position
    vec3  r       = m_rotation.drag_start;
    vec3  r_      = normalize(r);

    // p = local center
    vec3  p       = m_rotation.center_of_rotation;
    vec3  n       = m_rotation.normal;
    vec3  side    = m_rotation.side;
    vec3  up      = m_rotation.up;

    constexpr const float c_parallel_threshold = 0.4f;
    vec3  V0      = vec3(root()->position_in_world()) - vec3(pointer_context.camera->node()->position_in_world());
    vec3  V       = normalize(m_drag.initial_local_from_world * vec4(V0, 0.0f));
    float v_dot_n = dot(V, n);
    if (std::abs(v_dot_n) < c_parallel_threshold)
    {
        return update_rotate_parallel(pointer_context);
    }

    vec3 rotation_axis = get_axis_direction();
    vec4 initial_root_position_in_model = m_drag.initial_world_from_local * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    mat4 translate     = create_translation(vec3(-initial_root_position_in_model));
    mat4 untranslate   = create_translation(vec3( initial_root_position_in_model));
    //mat4 world_from_parent = (root()->parent != nullptr) ? root()->parent->world_from_node()
    //                                                    : mat4(1);
#if 0
    vec3 q0 = pointer_context.position_in_world(0.0f);
    vec3 q1 = pointer_context.position_in_world(1.0f);
    vec3  closest_point;
    float closest_angle = 0.0f;
    float closest_distance = std::numeric_limits<float>::max();
    for (float angle = 0.0f; angle < 2.0 * glm::pi<float>(); angle += glm::pi<float>() / 3600.0f)
    {
        mat4 rotation                 = create_rotation(angle, rotation_axis);
        mat4 parent_from_rotated_node = untranslate * rotation * translate * m_drag.initial_world_from_local;
        mat4 world_from_rotated_node  = world_from_parent * parent_from_rotated_node;
        vec3 position                 = vec3(world_from_rotated_node * vec4(m_drag.initial_position_in_world, 1.0f));
        float distance;
        if (line_point_distance(q0, q1, position, distance) && (distance < closest_distance))
        {
            closest_distance = distance;
            closest_angle    = angle;
            closest_point    = position;
        }
    }
    log_trs_tool.trace("closest angle = {: #08f} distance = {: #08f} point = {}\n", closest_angle, closest_distance, closest_point);
    mat4 rotation = create_rotation(closest_angle, rotation_axis);

    root()->transforms.parent_from_node.set(untranslate * rotation * translate * m_drag.initial_world_from_local);
    update_transforms();
#else
    glm::vec3 q;
    bool intersection_exists = project_pointer_to_plane(pointer_context, p, q);
    if (!intersection_exists)
    {
        return update_rotate_parallel(pointer_context);
    }

    vec3  q_ = normalize(q);

    float r_dot_side = dot(r_, side);
    float r_dot_up   = dot(r_, up);
    float q_dot_side = dot(q_, side);
    float q_dot_up   = dot(q_, up);
    float r_angle    = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
    float q_angle    = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);
    float angle      = q_angle - r_angle;
    mat4 rotation      = create_rotation(angle, rotation_axis);

    root()->transforms.parent_from_node.set(untranslate * rotation * translate * m_drag.initial_world_from_local);
    update_transforms();
#endif

}

void Trs_tool::update_rotate_parallel(Pointer_context& pointer_context)
{
    vec3 r    = vec3(m_drag.initial_local_from_world * vec4(m_drag.initial_position_in_world, 1.0f));
    vec3 p    = offset_plane_origo(m_active_handle, r);
    vec3 n    = get_plane_normal_in_model();
    vec3 side = get_plane_side_in_model();
    vec3 up   = cross(n, side);
    vec3 pw   = pointer_context.position_in_world(m_drag.initial_window_depth);
    vec3 q0   = vec3(m_drag.initial_local_from_world * vec4(pw, 1.0f));
    vec3 q    = project_to_offset_plane(m_active_handle, p, q0);

    r = normalize(r - p);
    q = normalize(q - p);

    float r_dot_side = dot(r, side);
    float r_dot_up   = dot(r, up);
    float q_dot_side = dot(q, side);
    float q_dot_up   = dot(q, up);

    float r_angle    = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
    float q_angle    = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);

    float angle      = q_angle - r_angle;

    vec3 rotation_axis = get_axis_direction();
    vec4 initial_root_position_in_model = m_drag.initial_world_from_local * vec4(0.0f, 0.0f, 0.0f, 1.0f);
    mat4 translate     = create_translation(vec3(-initial_root_position_in_model));
    mat4 untranslate   = create_translation(vec3( initial_root_position_in_model));
    mat4 rotation      = create_rotation(angle, rotation_axis);

    root()->transforms.parent_from_node.set(untranslate * rotation * translate * m_drag.initial_world_from_local);
    update_transforms();
}

void Trs_tool::render_update()
{
    auto&     camera                 = m_scene_manager->camera();
    glm::vec3 view_position_in_world = vec3(camera.node()->position_in_world());

    m_visualization.update_scale(view_position_in_world);
}

void Trs_tool::render(Render_context& render_context)
{
    ZoneScoped;

    if (m_state != State::active)
    {
        return;
    }

    return; // currently broken
    
    auto* line_renderer     = render_context.line_renderer;
    auto& camera            = m_scene_manager->camera();
    vec3  position_in_world = root()->position_in_world();
    float distance          = length(position_in_world - vec3(camera.node()->position_in_world()));
    float scale             = m_visualization.scale * distance / 100.0f;

    if ((get_handle_type(m_active_handle) == Handle_type::e_handle_type_rotate) &&
        (line_renderer != nullptr))
    {
        vec3 r     = vec3(m_drag.initial_local_from_world * vec4(m_drag.initial_position_in_world, 1.0f));
        vec3 p     = offset_plane_origo(m_active_handle, r);
        vec3 side0 = vec3(m_drag.initial_world_from_local * vec4(get_plane_side_in_model(), 0.0f));
        vec3 side1 = vec3(m_drag.initial_world_from_local * vec4(get_plane_side_in_model2(), 0.0f));
        vec3 q     = vec3(root()->node_from_world() * vec4(m_drag.initial_position_in_world, 1.0f));

        r = normalize(r - p);
        q = normalize(q - p);

        float r_dot_side = dot(r, side0);
        float r_dot_up   = dot(r, side1);
        float q_dot_side = dot(q, side0);
        float q_dot_up   = dot(q, side1);

        float r_angle = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
        float q_angle = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);

        //vec3 direction         = vec3(m_drag.initial_world_from_local * vec4(get_plane_normal_in_model(), 0.0f));
        //vec3 current_direction = vec3(root()->world_from_node()        * vec4(get_plane_side_in_model(), 0.0f));
        //vec3 root              = vec3(root()->position_in_world());
        vec3 root_position     =  vec3(root()->world_from_node()       * vec4(p, 1.0f));
        //vec3 neg_z             = root_position - 10.f * direction;
        //vec3 pos_z             = root_position + 10.f * direction;
        //line_renderer->set_line_color(0xffffccaau);
        line_renderer->set_line_color(0xffffffffu);

        float r1 = scale * 10.0f;
        {
            int sector_count = 90;
            std::vector<vec3> positions;
            for (int i = 0; i < sector_count + 1; ++i)
            {
                float rel   = static_cast<float>(i) / static_cast<float>(sector_count);
                float angle = rel * 2.0f * pi<float>();
                float r0    = (i == 0) ? 0.0f
                                       : (i % 10 == 0) ? 4.0f * scale 
                                                       : 8.0f * scale;

                angle += r_angle;
                vec3 p0 = root_position +
                          r0 * std::cos(angle) * side0 +
                          r0 * std::sin(angle) * side1;
                vec3 p1 = root_position +
                          r1 * std::cos(angle) * side0 +
                          r1 * std::sin(angle) * side1;
                line_renderer->add_lines( { {p0, p1} }, 10.0f );
            }
        }

        {
            int sector_count = 200;
            std::vector<vec3> positions;
            for (int i = 0; i < sector_count + 1; ++i)
            {
                float     rel   = static_cast<float>(i) / static_cast<float>(sector_count);
                float     angle = rel * 2.0f * pi<float>();
                angle += r_angle;
                positions.emplace_back(root_position +
                                       r1 * std::cos(angle) * side0 +
                                       r1 * std::sin(angle) * side1);
            }
            for (size_t i = 0, count = positions.size(); i < count; ++i)
            {
                size_t next_i = (i + 1) % count;
                line_renderer->add_lines( { {positions[i], positions[next_i]} }, 10.0f );
            }
        }

        line_renderer->set_line_color(0xff00ff00u);
        line_renderer->add_lines( { {root_position,
                                    r1 * std::sin(q_angle) * side1 +
                                    r1 * std::cos(q_angle) * side0
                                   } }, 10.0f );
        line_renderer->set_line_color(0xff0000ffu);
        line_renderer->add_lines( { {root_position,
                                    (r1 + 1.0f) * std::sin(r_angle) * side1 +
                                    (r1 + 1.0f) * std::cos(r_angle) * side0
                                   } }, 10.0f );
    }

    auto* text_renderer   = render_context.text_renderer;
    auto* pointer_context = render_context.pointer_context;
    if ((text_renderer != nullptr) &&
        (pointer_context != nullptr) &&
        (pointer_context->camera != nullptr))
    {
        //float depth_range_near  = 0.0f;
        //float depth_range_far   = 1.0f;
        //vec3  pointer_in_window = vec3(static_cast<float>(pointer_context->pointer_x),
        //                                                  static_cast<float>(pointer_context->pointer_y),
        //                                                  m_drag.initial_window_depth);
        //vec3 pointer_in_world = unproject(pointer_context->camera->world_from_clip(),
        //                                  pointer_in_window,
        //                                  depth_range_near,
        //                                  depth_range_far,
        //                                  static_cast<float>(pointer_context->viewport.x),
        //                                  static_cast<float>(pointer_context->viewport.y),
        //                                  static_cast<float>(pointer_context->viewport.width),
        //                                  static_cast<float>(pointer_context->viewport.height));
        //pointer_in_world = pointer_context->position_in_world();

        uint32_t text_color = 0xffffffffu; // abgr
        vec3 text_position = vec3(100.0f, 100.0f, -0.5f);
        text_renderer->print(fmt::format("p  = {}  {}", pointer_context->pointer_x, pointer_context->pointer_y), text_position, text_color);
        text_position.y -= 20.0f;
    }
}

auto Trs_tool::Rotation_context::angle_of_rotation_for_point(vec3 q) -> float
{
    vec3  q_         = normalize(q);
    float q_dot_side = dot(q_, side);
    float q_dot_up   = dot(q_, up);
    float angle      = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);
    return angle;
}

auto Trs_tool::begin(Pointer_context& pointer_context) -> bool
{
    if (!pointer_context.hover_mesh)
    {
        return false;
    }
    m_active_handle = get_handle(pointer_context.hover_mesh.get());
    if (m_active_handle == Handle::e_handle_none)
    {
        return false;
    }
    if (root() == nullptr)
    {
        return false;
    }
    m_drag.initial_position_in_world = pointer_context.position_in_world();
    m_drag.position_in_root          = root()->node_from_world() * vec4(m_drag.initial_position_in_world, 1.0f);
    m_drag.initial_world_from_local  = root()->world_from_node();
    m_drag.initial_local_from_world  = root()->node_from_world();
    m_drag.initial_window_depth      = pointer_context.pointer_z;
    if (m_target_node)
    {
        m_drag.initial_transforms = m_target_node->transforms;
    }

    // For rotation
    if (is_rotate_active())
    {
        vec3 rotation_axis_in_world = get_axis_direction();
        m_rotation.drag_start               = vec3(m_drag.initial_local_from_world * vec4(m_drag.initial_position_in_world, 1.0f));;
        m_rotation.center_of_rotation       = vec3(0.0f, 0.0f, 0.0f); // offset_plane_origo(m_active_handle, m_rotation.drag_start);
        m_rotation.normal                   = get_plane_normal_in_model();
        m_rotation.side                     = get_plane_side_in_model2();
        m_rotation.up                       = cross(m_rotation.normal, m_rotation.side);
        m_rotation.start_rotation_angle     = m_rotation.angle_of_rotation_for_point(m_rotation.drag_start);
        m_rotation.local_to_rotation_offset = create_rotation(m_rotation.start_rotation_angle, rotation_axis_in_world);
        m_rotation.rotation_offset_to_local = create_rotation(-m_rotation.start_rotation_angle, rotation_axis_in_world);
    }

    log_tools.trace("Trs tool state = Ready\n");
    m_state = State::ready;
    update_visibility();
    return true;
}

auto Trs_tool::end(Pointer_context& pointer_context) -> bool
{
    static_cast<void>(pointer_context);
    bool consume_event = m_state == State::active;
    cancel_ready();
    update_visibility();

    Node_transform_operation::Context context;
    context.scene_manager = m_scene_manager;
    context.node          = m_target_node;
    context.before        = m_drag.initial_transforms;
    context.after         = m_target_node->transforms;

    auto op = std::make_shared<Node_transform_operation>(context);
    m_operation_stack->push(op);
    return consume_event;
}

void Trs_tool::cancel_ready()
{
    log_tools.trace("Trs tool state = Passive\n");
    m_state                          = State::passive;
    m_active_handle                  = Handle::e_handle_none;
    m_drag.initial_position_in_world = vec3(0.0f, 0.0f, 0.0f);
    m_drag.initial_world_from_local  = mat4(1);
    m_drag.initial_window_depth      = 0.0f;
}

auto Trs_tool::get_handle(Mesh* mesh) const -> Trs_tool::Handle
    {
        auto i = m_handles.find(mesh);
        if (i == m_handles.end())
        {
            return Handle::e_handle_none;
        }
        return i->second;
    }

auto Trs_tool::get_handle_type(Handle handle) const -> Handle_type
{
    switch (handle)
    {
        case Handle::e_handle_translate_x:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_y:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_z:  return Handle_type::e_handle_type_translate_axis;
        case Handle::e_handle_translate_xy: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_xz: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_translate_yz: return Handle_type::e_handle_type_translate_plane;
        case Handle::e_handle_rotate_x:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_y:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_rotate_z:     return Handle_type::e_handle_type_rotate;
        case Handle::e_handle_none: return Handle_type::e_handle_type_none;
        default:
            FATAL("bad handle");
    }
}

auto Trs_tool::get_axis_color(Handle handle) const -> uint32_t
{
    switch (handle)
    {
        case Handle::e_handle_translate_x: return 0xff0000ff;
        case Handle::e_handle_translate_y: return 0xff00ff00;
        case Handle::e_handle_translate_z: return 0xffff0000;
        case Handle::e_handle_rotate_x:    return 0xff0000ff;
        case Handle::e_handle_rotate_y:    return 0xff00ff00;
        case Handle::e_handle_rotate_z:    return 0xffff0000;
        case Handle::e_handle_none:
        default:
            FATAL("bad handle");
    }
}

void Trs_tool::Visualization::update_transforms()
{
    ZoneScoped;

    x_arrow_cylinder_mesh->node->update();
    x_arrow_cone_mesh    ->node->update();
    y_arrow_cylinder_mesh->node->update();
    y_arrow_cone_mesh    ->node->update();
    z_arrow_cylinder_mesh->node->update();
    z_arrow_cone_mesh    ->node->update();
    xy_box_mesh          ->node->update();
    xz_box_mesh          ->node->update();
    yz_box_mesh          ->node->update();
    x_rotate_ring_mesh   ->node->update();
    y_rotate_ring_mesh   ->node->update();
    z_rotate_ring_mesh   ->node->update();
}

} // namespace editor
