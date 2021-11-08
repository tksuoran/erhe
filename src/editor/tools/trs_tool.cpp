#include "tools/trs_tool.hpp"
#include "graphics/gl_context_provider.hpp"
#include "log.hpp"
#include "tools.hpp"
#include "operations/operation_stack.hpp"
#include "operations/insert_operation.hpp"
#include "renderers/line_renderer.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/text_renderer.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_root.hpp"
#include "tools/pointer_context.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/verify.hpp"
#include "erhe/toolkit/tracy_client.hpp"

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

Trs_tool::Trs_tool()
    : erhe::components::Component{c_name}
{
}

Trs_tool::~Trs_tool() = default;

auto Trs_tool::description() -> const char*
{
    return c_name.data();
}

auto Trs_tool::state() const -> State
{
    return m_state;
}

void Trs_tool::set_translate(const bool enabled)
{
    m_visualization.show_translate = enabled;
    update_visibility();
}

void Trs_tool::set_rotate(const bool enabled)
{
    m_visualization.show_rotate = enabled;
    update_visibility();
}

void Trs_tool::set_node(const std::shared_ptr<Node>& node)
{
    if (node == m_target_node)
    {
        return;
    }

    auto* const rigid_body = m_node_physics 
        ? m_node_physics->rigid_body()
        : nullptr;

    // Attach new host to manipulator
    m_target_node = node;
    m_node_physics = node
        ? get_physics_node(node.get())
        : std::shared_ptr<Node_physics>();

    // Promote static to kinematic if we try to move object
    if (rigid_body != nullptr)
    {
        const auto motion_mode = rigid_body->get_motion_mode();
        if (motion_mode == erhe::physics::Motion_mode::e_static)
        {
            rigid_body->set_motion_mode(erhe::physics::Motion_mode::e_kinematic);
        }
        rigid_body->begin_move();
    }

    if (m_target_node)
    {
        m_visualization.root = m_target_node.get();
        //m_visualization.root->attach(m_visualization.tool_node);
        //m_visualization.tool_node.parent = m_visualization.root;
    }
    else
    {
        m_visualization.root = nullptr;
        //m_visualization.tool_node->unparent();
    }

    update_visibility();
}

Trs_tool::Visualization::Visualization()
{
    tool_node = std::make_shared<erhe::scene::Node>("Trs");
}

void Trs_tool::Visualization::update_scale(const vec3 view_position_in_world)
{
    ZoneScoped;

    if (root == nullptr)
    {
        return;
    }

    const vec3 position_in_world = root->position_in_world();
    view_distance = length(position_in_world - vec3{view_position_in_world});
}

void Trs_tool::Visualization::update_visibility(const bool visible, const Handle active_handle)
{
    ZoneScoped;

    constexpr uint64_t visible_mask = erhe::scene::Node::c_visibility_tool | erhe::scene::Node::c_visibility_id;
    const bool show_all = visible && (active_handle == Handle::e_handle_none);
    y_arrow_cylinder_mesh->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    z_arrow_cylinder_mesh->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    xy_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xy) || show_all) ? visible_mask : 0;
    xz_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xz) || show_all) ? visible_mask : 0;
    yz_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_yz) || show_all) ? visible_mask : 0;
    x_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_x    ) || show_all) ? visible_mask : 0;
    y_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_y    ) || show_all) ? visible_mask : 0;
    z_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_z    ) || show_all) ? visible_mask : 0;
    x_arrow_cylinder_mesh->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_x ) || show_all) ? visible_mask : 0;
    x_arrow_cone_mesh    ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_x ) || show_all) ? visible_mask : 0;
    y_arrow_cylinder_mesh->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    y_arrow_cone_mesh    ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_y ) || show_all) ? visible_mask : 0;
    z_arrow_cylinder_mesh->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    z_arrow_cone_mesh    ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_z ) || show_all) ? visible_mask : 0;
    xy_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xy) || show_all) ? visible_mask : 0;
    xz_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_xz) || show_all) ? visible_mask : 0;
    yz_box_mesh          ->visibility_mask() = show_translate && (!hide_inactive || (active_handle == Handle::e_handle_translate_yz) || show_all) ? visible_mask : 0;
    x_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_x    ) || show_all) ? visible_mask : 0;
    y_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_y    ) || show_all) ? visible_mask : 0;
    z_rotate_ring_mesh   ->visibility_mask() = show_rotate    && (!hide_inactive || (active_handle == Handle::e_handle_rotate_z    ) || show_all) ? visible_mask : 0;

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

void Trs_tool::Visualization::initialize(Mesh_memory& mesh_memory, Scene_root& scene_root)
{
    x_material         = scene_root.make_material("x",         vec4{1.00f, 0.00f, 0.0f, 1.0f});
    y_material         = scene_root.make_material("y",         vec4{0.23f, 1.00f, 0.0f, 1.0f});
    z_material         = scene_root.make_material("z",         vec4{0.00f, 0.23f, 1.0f, 1.0f});
    highlight_material = scene_root.make_material("highlight", vec4{1.00f, 0.70f, 0.1f, 1.0f});

    constexpr float arrow_cylinder_length    = 2.5f;
    constexpr float arrow_cone_length        = 1.0f;
    constexpr float arrow_cylinder_radius    = 0.08f;
    constexpr float arrow_cone_radius        = 0.35f;
    constexpr float box_half_thickness       = 0.02f;
    constexpr float box_length               = 1.0f;
    constexpr float arrow_tip                = arrow_cylinder_length + arrow_cone_length;
    constexpr float rotate_ring_major_radius = 4.0f;
    constexpr float rotate_ring_minor_radius = 0.25f;
    const auto arrow_cylinder_geometry  = make_cylinder(0.0, arrow_cylinder_length, arrow_cylinder_radius, true, true, 32, 4);
    const auto arrow_cone_geometry      = make_cone    (arrow_cylinder_length, arrow_tip, arrow_cone_radius, true, 32, 4);
    const auto box_geometry             = make_box     (0.0, box_length, 0.0, box_length, -box_half_thickness, box_half_thickness);
    auto       rotate_ring_geometry     = make_torus   (rotate_ring_major_radius, rotate_ring_minor_radius, 80, 32);
    // Torus geometry is on xz plane, swap x and y to make it yz plane
    rotate_ring_geometry.transform(mat4_swap_xy);
    rotate_ring_geometry.reverse_polygons();

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;
    auto  arrow_cylinder_pg = make_primitive_shared(arrow_cylinder_geometry, mesh_memory.build_info_set.gl);
    auto  arrow_cone_pg     = make_primitive_shared(arrow_cone_geometry    , mesh_memory.build_info_set.gl);
    auto  box_pg            = make_primitive_shared(box_geometry           , mesh_memory.build_info_set.gl);
    auto  rotate_ring_pg    = make_primitive_shared(rotate_ring_geometry   , mesh_memory.build_info_set.gl);
    auto& tool_layer        = *scene_root.tool_layer().get();
    const vec3 pos{0.0f, 1.0f, 0.0f};
    auto* parent = tool_node.get();
    x_arrow_cylinder_mesh  = scene_root.make_mesh_node("X arrow cylinder", arrow_cylinder_pg, x_material, tool_layer, parent, pos);
    x_arrow_cone_mesh      = scene_root.make_mesh_node("X arrow cone",     arrow_cone_pg,     x_material, tool_layer, parent, pos);
    y_arrow_cylinder_mesh  = scene_root.make_mesh_node("Y arrow cylinder", arrow_cylinder_pg, y_material, tool_layer, parent, pos);
    y_arrow_cone_mesh      = scene_root.make_mesh_node("Y arrow cone",     arrow_cone_pg,     y_material, tool_layer, parent, pos);
    z_arrow_cylinder_mesh  = scene_root.make_mesh_node("Z arrow cylinder", arrow_cylinder_pg, z_material, tool_layer, parent, pos);
    z_arrow_cone_mesh      = scene_root.make_mesh_node("Z arrow cone",     arrow_cone_pg,     z_material, tool_layer, parent, pos);
    xy_box_mesh            = scene_root.make_mesh_node("XY box",           box_pg,            z_material, tool_layer, parent, pos);
    xz_box_mesh            = scene_root.make_mesh_node("XZ box",           box_pg,            y_material, tool_layer, parent, pos);
    yz_box_mesh            = scene_root.make_mesh_node("YZ box",           box_pg,            x_material, tool_layer, parent, pos);
    x_rotate_ring_mesh     = scene_root.make_mesh_node("X rotate ring",    rotate_ring_pg,    x_material, tool_layer, parent, pos);
    y_rotate_ring_mesh     = scene_root.make_mesh_node("Y rotate ring",    rotate_ring_pg,    y_material, tool_layer, parent, pos);
    z_rotate_ring_mesh     = scene_root.make_mesh_node("Z rotate ring",    rotate_ring_pg,    z_material, tool_layer, parent, pos);

    x_arrow_cylinder_mesh->set_parent_from_node(mat4{1});
    x_arrow_cone_mesh    ->set_parent_from_node(mat4{1});
    y_arrow_cylinder_mesh->set_parent_from_node(Transform::create_rotation( pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f}));
    y_arrow_cone_mesh    ->set_parent_from_node(Transform::create_rotation( pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f}));
    z_arrow_cylinder_mesh->set_parent_from_node(Transform::create_rotation(-pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f}));
    z_arrow_cone_mesh    ->set_parent_from_node(Transform::create_rotation(-pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f}));
    xy_box_mesh          ->set_parent_from_node(mat4{1});
    xz_box_mesh          ->set_parent_from_node(Transform::create_rotation( pi<float>() / 2.0f, vec3{1.0f, 0.0f, 0.0f}));
    yz_box_mesh          ->set_parent_from_node(Transform::create_rotation(-pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f}));

    x_rotate_ring_mesh->set_parent_from_node(mat4{1});
    y_rotate_ring_mesh->set_parent_from_node(Transform::create_rotation( pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f}));
    z_rotate_ring_mesh->set_parent_from_node(Transform::create_rotation(-pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f}));

    //root->transforms.parent_from_node.set_rotation(pi<float>() / 4.0f, vec3{0.0f, 1.0f, 0.0f});

    update_visibility(false, Handle::e_handle_none);
}

void Trs_tool::connect()
{
    require<Gl_context_provider>();

    m_operation_stack = get<Operation_stack>();
    m_mesh_memory     = require<Mesh_memory>();
    m_scene_root      = require<Scene_root>();
    m_selection_tool  = require<Selection_tool>();
}

void Trs_tool::initialize_component()
{
    ZoneScoped;

    Scoped_gl_context gl_context{Component::get<Gl_context_provider>().get()};

    VERIFY(m_mesh_memory);
    VERIFY(m_scene_root);

    m_visualization.initialize(*m_mesh_memory.get(), *m_scene_root.get());
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

    if (m_selection_tool)
    {
        auto lambda = [this](const Selection_tool::Selection& selection)
        {
            if (selection.empty())
            {
                set_node({});
            }
            else
            {
                set_node(selection.front());
            }
        };
        m_selection_subscription = m_selection_tool->subscribe_selection_change_notification(lambda);
    }

    get<Editor_tools>()->register_tool(this);
}

void Trs_tool::imgui(Pointer_context&)
{
    ZoneScoped;

    const bool show_translate = m_visualization.show_translate;
    const bool show_rotate    = m_visualization.show_rotate;
    ImGui::Begin("Transform");

    const auto button_size = ImVec2(ImGui::GetContentRegionAvailWidth(), 0.0f);

    if (make_button("Local", (m_local) ? Item_mode::active : Item_mode::normal, button_size))
    {
        m_local = true;
        m_visualization.local = true;
    }
    if (make_button("Global", (!m_local) ? Item_mode::active : Item_mode::normal, button_size))
    {
        m_local = false;
        m_visualization.local = false;
    }

    ImGui::SliderFloat("Scale",                 &m_visualization.scale, 1.0f, 10.0f);

    {
        ImGui::Checkbox("Translate Tool",        &m_visualization.show_translate);
        ImGui::Checkbox("Translate Snap Enable", &m_translate_snap_enable);
        const float translate_snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
        const char* translate_snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
        make_combo("Translate Snap", m_translate_snap_index, translate_snap_items, IM_ARRAYSIZE(translate_snap_items));
        if (
            (m_translate_snap_index >= 0) &&
            (m_translate_snap_index < IM_ARRAYSIZE(translate_snap_values))
        )
        {
            m_translate_snap = translate_snap_values[m_translate_snap_index];
        }
    }

    ImGui::Separator();

    {
        ImGui::Checkbox("Rotate Tool",        &m_visualization.show_rotate);
        ImGui::Checkbox("Rotate Snap Enable", &m_rotate_snap_enable);
        const float rotate_snap_values[] = {  5.0f, 10.0f, 15.0f, 20.0f, 30.0f, 45.0f, 60.0f, 90.0f };
        const char* rotate_snap_items [] = { "5",  "10",  "15",  "20",  "30",  "45",  "60",  "90" };
        make_combo("Rotate Snap", m_rotate_snap_index, rotate_snap_items, IM_ARRAYSIZE(rotate_snap_items));
        if (
            (m_rotate_snap_index >= 0) &&
            (m_rotate_snap_index < IM_ARRAYSIZE(rotate_snap_values))
        )
        {
            m_rotate_snap = rotate_snap_values[m_rotate_snap_index];
        }
    }

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

    if ((m_state == State::Passive) && pointer_context.mouse_button[Mouse_button_left].pressed)
    {
        return begin(pointer_context);
    }
    if ((m_state != State::Passive) && pointer_context.mouse_button[Mouse_button_left].released)
    {
        return end(pointer_context);
    }
    if ((m_state == State::Ready) && pointer_context.mouse_moved)
    {
        log_tools.trace("Trs tool state = Active\n");
        m_state = State::Active;
    }
    if (m_state != State::Active)
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
        case Handle::e_handle_translate_x:  return m_local ? m_drag.initial_world_from_local[0] : glm::vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_translate_y:  return m_local ? m_drag.initial_world_from_local[1] : glm::vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_translate_z:  return m_local ? m_drag.initial_world_from_local[2] : glm::vec3{0.0f, 0.0f, 1.0f};
        case Handle::e_handle_translate_yz: return m_local ? m_drag.initial_world_from_local[0] : glm::vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_translate_xz: return m_local ? m_drag.initial_world_from_local[1] : glm::vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_translate_xy: return m_local ? m_drag.initial_world_from_local[2] : glm::vec3{0.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_x:     return m_local ? m_drag.initial_world_from_local[0] : glm::vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y:     return m_local ? m_drag.initial_world_from_local[1] : glm::vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_rotate_z:     return m_local ? m_drag.initial_world_from_local[2] : glm::vec3{0.0f, 0.0f, 1.0f};
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
    bool use_ss = closest_point(
        vec2{ss_P0},
        vec2{ss_P1},
        vec2{
            static_cast<float>(pointer_context.pointer_x),
            static_cast<float>(pointer_context.pointer_y)
        },
        ss_closest);
    vec3 R0 = pointer_context.position_in_world(vec3{ss_closest, 0.0f});
    vec3 R1 = pointer_context.position_in_world(vec3{ss_closest, 1.0f});
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

    mat4 world_from_node = translation * m_drag.initial_world_from_local;
    set_node_world_transform(world_from_node);
    update_transforms();
}

auto Trs_tool::is_x_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_x ) ||
        (m_active_handle == Handle::e_handle_translate_xy) ||
        (m_active_handle == Handle::e_handle_translate_xz);
}

auto Trs_tool::is_y_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_y ) ||
        (m_active_handle == Handle::e_handle_translate_xy) ||
        (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_z_translate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_translate_z ) ||
        (m_active_handle == Handle::e_handle_translate_xz) ||
        (m_active_handle == Handle::e_handle_translate_yz);
}

auto Trs_tool::is_rotate_active() const -> bool
{
    return
        (m_active_handle == Handle::e_handle_rotate_x) ||
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
        case Handle::e_handle_translate_xy: return m_local ? m_drag.initial_world_from_local[2] : glm::vec3{0.0f, 0.0f, 1.0f};
        case Handle::e_handle_translate_xz: return m_local ? m_drag.initial_world_from_local[1] : glm::vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_translate_yz: return m_local ? m_drag.initial_world_from_local[0] : glm::vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_x:     return m_local ? m_drag.initial_world_from_local[0] : glm::vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y:     return m_local ? m_drag.initial_world_from_local[1] : glm::vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_rotate_z:     return m_local ? m_drag.initial_world_from_local[2] : glm::vec3{0.0f, 0.0f, 1.0f};
        default:
            FATAL("bad handle for plane");
            break;
    }
}

auto Trs_tool::get_plane_normal_in_model() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_rotate_x: return vec3{1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y: return vec3{0.0f, 1.0f, 0.0f};
        case Handle::e_handle_rotate_z: return vec3{0.0f, 0.0f, 1.0f};
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
        case Handle::e_handle_rotate_x: return vec3{ 0.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_y: return vec3{ 0.0f, 0.0f, 1.0f};
        case Handle::e_handle_rotate_z: return vec3{-1.0f, 0.0f, 0.0f};
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::get_plane_side_in_model2() const -> vec3
{
    switch (m_active_handle)
    {
        case Handle::e_handle_rotate_x: return vec3{ 0.0f, 1.0f, 0.0f};
        case Handle::e_handle_rotate_y: return vec3{ 1.0f, 0.0f, 0.0f};
        case Handle::e_handle_rotate_z: return vec3{ 0.0f, 1.0f, 0.0f};
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

    mat4 world_from_node = translation * m_drag.initial_world_from_local;
    set_node_world_transform(world_from_node);
    update_transforms();
}

auto Trs_tool::offset_plane_origo(const Handle handle, const vec3 p) const -> vec3
{
    switch (handle)
    {
        case Handle::e_handle_rotate_x: return vec3{ p.x, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y: return vec3{0.0f,  p.y, 0.0f};
        case Handle::e_handle_rotate_z: return vec3{0.0f, 0.0f,  p.z};
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::project_to_offset_plane(const Handle handle, const vec3 p, const vec3 q) const -> vec3
{
    switch (handle)
    {
        case Handle::e_handle_rotate_x: return vec3{p.x, q.y, q.z};
        case Handle::e_handle_rotate_y: return vec3{q.x, p.y, q.z};
        case Handle::e_handle_rotate_z: return vec3{q.x, q.y, p.z};
        default:
            FATAL("bad handle for rotate");
            break;
    }
}

auto Trs_tool::project_pointer_to_plane(const Pointer_context& pointer_context, const vec3 p, vec3& q) -> bool
{
    const vec3 n  = get_plane_normal_in_model();
    const vec3 q0 = vec3{m_drag.initial_local_from_world * vec4{pointer_context.position_in_world(0.0f), 1.0f}};
    const vec3 q1 = vec3{m_drag.initial_local_from_world * vec4{pointer_context.position_in_world(1.0f), 1.0f}};
    const vec3 v  = normalize(q1 - q0);
    float      t  = 0.0f;
    const bool ok = intersect_plane(n, p, q0, v, t);
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
    const vec3  r       = m_rotation.drag_start;
    const vec3  r_      = normalize(r);

    // p = local center
    const vec3  p       = m_rotation.center_of_rotation;
    const vec3  n       = m_rotation.normal;
    const vec3  side    = m_rotation.side;
    const vec3  up      = m_rotation.up;

    constexpr float c_parallel_threshold = 0.4f;
    const vec3  V0      = vec3{root()->position_in_world()} - vec3{pointer_context.camera->position_in_world()};
    const vec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0f});
    const float v_dot_n = dot(V, n);
    if (std::abs(v_dot_n) < c_parallel_threshold)
    {
        return update_rotate_parallel(pointer_context);
    }

    const vec3 rotation_axis                  = get_axis_direction();
    const vec4 initial_root_position_in_model = m_drag.initial_world_from_local * vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const mat4 translate     = create_translation(vec3{-initial_root_position_in_model});
    const mat4 untranslate   = create_translation(vec3{ initial_root_position_in_model});
    //mat4 world_from_parent = (root()->parent != nullptr) ? root()->parent->world_from_node()
    //                                                     : mat4{1};
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
        vec3 position                 = vec3{world_from_rotated_node * vec4{m_drag.initial_position_in_world, 1.0f}}    ;
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
    const bool intersection_exists = project_pointer_to_plane(pointer_context, p, q);
    if (!intersection_exists)
    {
        return update_rotate_parallel(pointer_context);
    }

    const vec3  q_ = normalize(q);

    const float r_dot_side      = dot(r_, side);
    const float r_dot_up        = dot(r_, up);
    const float q_dot_side      = dot(q_, side);
    const float q_dot_up        = dot(q_, up);
    const float r_angle         = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
    const float q_angle         = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);
    const float angle           = q_angle - r_angle;
    const mat4  rotation        = create_rotation(angle, rotation_axis);
    const mat4  world_from_node = untranslate * rotation * translate * m_drag.initial_world_from_local;
    set_node_world_transform(world_from_node);
    update_transforms();
#endif

}

void Trs_tool::update_rotate_parallel(Pointer_context& pointer_context)
{
    const vec3 r_   = vec3{m_drag.initial_local_from_world * vec4{m_drag.initial_position_in_world, 1.0f}};
    const vec3 p    = offset_plane_origo(m_active_handle, r_);
    const vec3 n    = get_plane_normal_in_model();
    const vec3 side = get_plane_side_in_model();
    const vec3 up   = cross(n, side);
    const vec3 pw   = pointer_context.position_in_world(m_drag.initial_window_depth);
    const vec3 q0   = vec3{m_drag.initial_local_from_world * vec4{pw, 1.0f}};
    const vec3 q_   = project_to_offset_plane(m_active_handle, p, q0);

    const vec3 r = normalize(r_ - p);
    const vec3 q = normalize(q_ - p);

    const float r_dot_side = dot(r, side);
    const float r_dot_up   = dot(r, up);
    const float q_dot_side = dot(q, side);
    const float q_dot_up   = dot(q, up);

    const float r_angle    = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
    const float q_angle    = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);

    const float angle      = q_angle - r_angle;

    const vec3 rotation_axis   = get_axis_direction();
    const vec4 initial_root_position_in_model = m_drag.initial_world_from_local * vec4{0.0f, 0.0f, 0.0f, 1.0f};
    const mat4 translate       = create_translation(vec3{-initial_root_position_in_model});
    const mat4 untranslate     = create_translation(vec3{ initial_root_position_in_model});
    const mat4 rotation        = create_rotation(angle, rotation_axis);
    const mat4 world_from_node = untranslate * rotation * translate * m_drag.initial_world_from_local;
    set_node_world_transform(world_from_node);
    update_transforms();
}

void Trs_tool::set_node_world_transform(const glm::mat4 world_from_node)
{
    const mat4 parent_from_world = (root()->parent() != nullptr)
        ? root()->parent()->node_from_world() * world_from_node
        : world_from_node;
    root()->set_parent_from_node(parent_from_world);
 }

void Trs_tool::render_update(const Render_context& render_context)
{
    const auto* camera = render_context.pointer_context->camera;
    if (camera == nullptr)
    {
        return;
    }

    const glm::vec3 view_position_in_world = vec3{camera->position_in_world()};

    m_visualization.update_scale(view_position_in_world);
    update_transforms();
}

void Trs_tool::render(const Render_context& /*render_context*/)
{
    ZoneScoped;

    if (m_state != State::Active)
    {
        return;
    }

    return; // currently broken
#if 0
    auto* line_renderer     = render_context.line_renderer;
    auto& camera            = *render_context.pointer_context->camera;
    vec3  position_in_world = root()->position_in_world();
    float distance          = length(position_in_world - vec3{camera.position_in_world()});
    float scale             = m_visualization.scale * distance / 100.0f;

    if ((get_handle_type(m_active_handle) == Handle_type::e_handle_type_rotate) &&
        (line_renderer != nullptr))
    {
        vec3 r     = vec3{m_drag.initial_local_from_world * vec4{m_drag.initial_position_in_world, 1.0f}};
        vec3 p     = offset_plane_origo(m_active_handle, r);
        vec3 side0 = vec3{m_drag.initial_world_from_local * vec4{get_plane_side_in_model(), 0.0f}};
        vec3 side1 = vec3{m_drag.initial_world_from_local * vec4{get_plane_side_in_model2(), 0.0f}};
        vec3 q     = vec3{root()->node_from_world()       * vec4{m_drag.initial_position_in_world, 1.0f}};

        r = normalize(r - p);
        q = normalize(q - p);

        float r_dot_side = dot(r, side0);
        float r_dot_up   = dot(r, side1);
        float q_dot_side = dot(q, side0);
        float q_dot_up   = dot(q, side1);

        float r_angle = (r_dot_up > 0.0f) ? std::acos(r_dot_side) : 2.0f * pi<float>() - std::acos(r_dot_side);
        float q_angle = (q_dot_up > 0.0f) ? std::acos(q_dot_side) : 2.0f * pi<float>() - std::acos(q_dot_side);

        //vec3 direction         = vec3{m_drag.initial_world_from_local * vec4{get_plane_normal_in_model(), 0.0f}};
        //vec3 current_direction = vec3{root()->world_from_node()       * vec4{get_plane_side_in_model(), 0.0f}};
        //vec3 root              = vec3{root()->position_in_world());
        vec3 root_position     =  vec3{root()->world_from_node()       * vec4{p, 1.0f}};
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
                float r0    =
                    (i == 0) ? 0.0f
                             : (i % 10 == 0) ? 4.0f * scale
                                             : 8.0f * scale;

                angle += r_angle;

                vec3 p0 =
                    root_position +
                    r0 * std::cos(angle) * side0 +
                    r0 * std::sin(angle) * side1;

                vec3 p1 =
                    root_position +
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
                positions.emplace_back(
                    root_position +
                    r1 * std::cos(angle) * side0 +
                    r1 * std::sin(angle) * side1
                );
            }
            for (size_t i = 0, count = positions.size(); i < count; ++i)
            {
                size_t next_i = (i + 1) % count;
                line_renderer->add_lines(
                    { {positions[i], positions[next_i]} }, 10.0f
                );
            }
        }

        line_renderer->set_line_color(0xff00ff00u);
        line_renderer->add_lines(
            {
                {
                    root_position,
                    r1 * std::sin(q_angle) * side1 +
                    r1 * std::cos(q_angle) * side0
                }
            },
            10.0f
        );
        line_renderer->set_line_color(0xff0000ffu);
        line_renderer->add_lines(
            {
                {
                    root_position,
                    (r1 + 1.0f) * std::sin(r_angle) * side1 +
                    (r1 + 1.0f) * std::cos(r_angle) * side0
                }
            },
            10.0f
        );
    }

    auto* text_renderer   = render_context.text_renderer;
    auto* pointer_context = render_context.pointer_context;
    if ((text_renderer != nullptr) &&
        (pointer_context != nullptr) &&
        (pointer_context->camera != nullptr))
    {
        //float depth_range_near  = 0.0f;
        //float depth_range_far   = 1.0f;
        //vec3  pointer_in_window = vec3{static_cast<float>(pointer_context->pointer_x),
        //                                                  static_cast<float>(pointer_context->pointer_y),
        //                                                  m_drag.initial_window_depth};
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
        vec3 text_position = vec3{100.0f, 100.0f, -0.5f};
        text_renderer->print(
            text_position,
            text_color,
            fmt::format(
                "p  = {}  {}",
                pointer_context->pointer_x,
                pointer_context->pointer_y
            )
        );
        text_position.y -= 20.0f;
    }
#endif
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
    m_drag.position_in_root          = root()->node_from_world() * vec4{m_drag.initial_position_in_world, 1.0f};
    m_drag.initial_world_from_local  = root()->world_from_node();
    m_drag.initial_local_from_world  = root()->node_from_world();
    m_drag.initial_window_depth      = pointer_context.pointer_z;
    if (m_target_node)
    {
        m_drag.initial_parent_from_node_transform = m_target_node->parent_from_node_transform();
    }

    // For rotation
    if (is_rotate_active())
    {
        vec3 rotation_axis_in_world = get_axis_direction();
        m_rotation.drag_start               = vec3{m_drag.initial_local_from_world * vec4{m_drag.initial_position_in_world, 1.0f}};
        m_rotation.center_of_rotation       = vec3{0.0f, 0.0f, 0.0f}; // offset_plane_origo(m_active_handle, m_rotation.drag_start);
        m_rotation.normal                   = get_plane_normal_in_model();
        m_rotation.side                     = get_plane_side_in_model2();
        m_rotation.up                       = cross(m_rotation.normal, m_rotation.side);
        m_rotation.start_rotation_angle     = m_rotation.angle_of_rotation_for_point(m_rotation.drag_start);
        m_rotation.local_to_rotation_offset = create_rotation(m_rotation.start_rotation_angle, rotation_axis_in_world);
        m_rotation.rotation_offset_to_local = create_rotation(-m_rotation.start_rotation_angle, rotation_axis_in_world);
    }

    log_tools.trace("Trs tool state = Ready\n");
    m_state = State::Ready;
    update_visibility();

    if (m_node_physics)
    {
        auto* const rigid_body = m_node_physics->rigid_body();
        if (rigid_body != nullptr)
        {
            rigid_body->begin_move();
        }
    }

    return true;
}

auto Trs_tool::end(Pointer_context& pointer_context) -> bool
{
    static_cast<void>(pointer_context);
    bool consume_event = m_state == State::Active;
    cancel_ready();
    update_visibility();

    Node_transform_operation::Context context{
        m_scene_root->content_layer(),
        m_scene_root->scene(),
        m_scene_root->physics_world(),
        m_target_node,
        m_drag.initial_parent_from_node_transform,
        m_target_node->parent_from_node_transform()
    };

    auto op = std::make_shared<Node_transform_operation>(context);
    m_operation_stack->push(op);

    if (m_node_physics)
    {
        auto* const rigid_body = m_node_physics->rigid_body();
        rigid_body->end_move();
    }

    return consume_event;
}

void Trs_tool::cancel_ready()
{
    log_tools.trace("Trs tool state = Passive\n");
    m_state                          = State::Passive;
    m_active_handle                  = Handle::e_handle_none;
    m_drag.initial_position_in_world = vec3{0.0f, 0.0f, 0.0f};
    m_drag.initial_world_from_local  = mat4{1};
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

auto Trs_tool::get_handle_type(const Handle handle) const -> Handle_type
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

auto Trs_tool::get_axis_color(const Handle handle) const -> uint32_t
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

void Trs_tool::Visualization::update_transforms(uint64_t serial)
{
    ZoneScoped;

    if (root == nullptr)
    {
        return;
    }

    auto world_from_root_transform = local
        ? root->world_from_node_transform()
        : Transform::create_translation(root->position_in_world());

    const mat4 scaling = erhe::toolkit::create_scale(scale * view_distance / 100.0f);
    world_from_root_transform.catenate(scaling);

    tool_node->set_parent_from_node(world_from_root_transform);
    tool_node->update_transform_recursive(serial);
}

void Trs_tool::update_transforms()
{
    if (root() == nullptr)
    {
        return;
    }
    const auto serial = m_scene_root->scene().transform_update_serial();
    root()->update_transform(serial);
    if (m_node_physics)
    {
        m_node_physics->on_node_updated();
    }
    m_visualization.update_transforms(serial);
}

void Trs_tool::update_visibility()
{
    m_visualization.update_visibility(m_target_node != nullptr, m_active_handle);
    update_transforms();
}

auto Trs_tool::root() -> erhe::scene::Node*
{
    return m_visualization.root;
}

} // namespace editor
