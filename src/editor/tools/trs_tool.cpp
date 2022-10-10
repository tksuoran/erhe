#include "tools/trs_tool.hpp"

#include "editor_log.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "graphics/icon_set.hpp"
#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "renderers/mesh_memory.hpp"
#include "renderers/render_context.hpp"
#include "scene/helpers.hpp"
#include "scene/material_library.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"
#include "tools/tools.hpp"
#include "windows/operations.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/commands/commands.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/renderers/line_renderer.hpp"
#include "erhe/application/renderers/text_renderer.hpp"
#include "erhe/application/view.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/physics/irigid_body.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/raytrace/iscene.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

[[nodiscard]] auto Trs_tool::c_str(const Handle handle) -> const char*
{
    switch (handle)
    {
        case Handle::e_handle_none        : return "None";
        case Handle::e_handle_translate_x : return "Translate X";
        case Handle::e_handle_translate_y : return "Translate Y";
        case Handle::e_handle_translate_z : return "Translate Z";
        case Handle::e_handle_translate_xy: return "Translate XY";
        case Handle::e_handle_translate_xz: return "Translate XZ";
        case Handle::e_handle_translate_yz: return "Translate YZ";
        case Handle::e_handle_rotate_x    : return "Rotate X";
        case Handle::e_handle_rotate_y    : return "Rotate Y";
        case Handle::e_handle_rotate_z    : return "Rotate Z";
        default: return "?";
    };
}

using glm::normalize;
using glm::cross;
using glm::dot;
using glm::distance;
using glm::dmat4;
using glm::dvec2;
using glm::dvec3;
using glm::dvec4;
using glm::mat4;
using glm::vec3;
using glm::vec4;

void Trs_tool_drag_command::try_ready(
    erhe::application::Command_context& context
)
{
    //log_trs_tool->trace("try_ready");
    if (!m_trs_tool.is_enabled())
    {
        //log_trs_tool->trace("trs not enabled");
        return;
    }

    if (m_trs_tool.on_drag_ready(context))
    {
        set_ready(context);
    }
}

auto Trs_tool_drag_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    //log_trs_tool->trace("try_call");
    if (!m_trs_tool.is_enabled())
    {
        //log_trs_tool->trace("trs not enabled");
        return false;
    }

    if (
        (get_tool_state() == erhe::application::State::Ready) &&
        m_trs_tool.is_enabled()
    )
    {
        set_active(context);
    }

    if (get_tool_state() != erhe::application::State::Active)
    {
        // We might be ready, but not consuming event yet
        return false;
    }

    const bool still_active = m_trs_tool.on_drag(context);
    if (!still_active)
    {
        set_inactive(context);
    }
    return still_active;
}

void Trs_tool_drag_command::on_inactive(
    erhe::application::Command_context& context
)
{
    static_cast<void>(context);
    log_trs_tool->trace("TRS on_inactive");

    if (get_tool_state() != erhe::application::State::Inactive)
    {
        m_trs_tool.end_drag(context);
    }
}

Trs_tool::Trs_tool()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title, c_type_name}
    , m_drag_command             {*this}
    , m_visualization            {*this}
{
}

Trs_tool::~Trs_tool() noexcept
{
}

auto Trs_tool::description() -> const char*
{
    return c_title.data();
}

void Trs_tool::declare_required_components()
{
    require<erhe::application::Commands           >();
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    m_editor_scenes  = require<Editor_scenes >();
    m_mesh_memory    = require<Mesh_memory   >();
    m_selection_tool = require<Selection_tool>();
    m_tools          = require<Tools         >();
}

void Trs_tool::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const erhe::application::Scoped_gl_context gl_context{
        Component::get<erhe::application::Gl_context_provider>()
    };

    ERHE_VERIFY(m_mesh_memory);
    ERHE_VERIFY(m_tools);

    const auto& tool_scene_root = m_tools->get_tool_scene_root().lock();
    if (!tool_scene_root)
    {
        return;
    }
    m_visualization.initialize(*Component::get<erhe::application::Configuration>(), *m_mesh_memory, tool_scene_root.get());
    m_handles[m_visualization.x_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_visualization.x_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_visualization.x_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_visualization.y_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_visualization.y_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_visualization.y_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_visualization.z_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_visualization.z_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_visualization.z_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_z;
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

    const auto& tools = get<Tools>();
    tools->register_tool(this);
    tools->register_hover_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    const auto commands = get<erhe::application::Commands>();
    commands->register_command(&m_drag_command);
    commands->bind_command_to_mouse_drag(&m_drag_command, erhe::toolkit::Mouse_button_left);
    commands->bind_command_to_controller_trigger(&m_drag_command, 0.50f, 0.45f, true);
}

void Trs_tool::post_initialize()
{
    m_line_renderer_set = get<erhe::application::Line_renderer_set>();
    m_text_renderer     = get<erhe::application::Text_renderer    >();
    m_icon_set          = get<Icon_set        >();
    m_operation_stack   = get<Operation_stack >();
    m_viewport_windows  = get<Viewport_windows>();
}

[[nodiscard]] auto Trs_tool::is_active() const -> bool
{
    return m_active_handle != Handle::e_handle_none;
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

auto Trs_tool::get_target_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return m_target_node;
}

void Trs_tool::set_node(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    if (node == m_target_node)
    {
        return;
    }

    log_trs_tool->trace("TRS set_node(node = {})", node ? node->name() : "");

    if (
        m_node_physics &&
        m_node_physics->rigid_body() &&
        m_original_motion_mode.has_value()
    )
    {
        log_trs_tool->trace("TRS restoring old physics node");
        m_node_physics->rigid_body()->set_motion_mode(m_original_motion_mode.value());
        m_original_motion_mode.reset();
    }

    // Attach new host to manipulator
    m_target_node = node;
    m_node_physics = node
        ? get_physics_node(node.get())
        : std::shared_ptr<Node_physics>();

    auto* const rigid_body = m_node_physics
        ? m_node_physics->rigid_body()
        : nullptr;

    if (rigid_body != nullptr)
    {
        log_trs_tool->trace("TRS node has rigid_body");
        m_original_motion_mode = rigid_body->get_motion_mode();
        rigid_body->set_motion_mode(m_motion_mode);
        rigid_body->begin_move();
    }

    if (m_target_node)
    {
        log_trs_tool->trace("TRS now has target node");
        m_visualization.root = m_target_node.get();
    }
    else
    {
        log_trs_tool->trace("TRS now does not have target");
        m_visualization.root = nullptr;
    }

    update_visibility();
}

Trs_tool::Visualization::Visualization(Trs_tool& trs_tool)
    : trs_tool{trs_tool}
{
}

void Trs_tool::Visualization::update_scale(
    const vec3 view_position_in_world
)
{
    if (root == nullptr)
    {
        return;
    }

    const vec3 position_in_world = root->position_in_world();
    view_distance = length(position_in_world - vec3{view_position_in_world});
}

auto Trs_tool::Visualization::get_handle_visibility(const Handle handle) const -> bool
{
    switch (trs_tool.get_handle_type(handle))
    {
        case Handle_type::e_handle_type_translate_axis:  return show_translate;
        case Handle_type::e_handle_type_translate_plane: return show_translate;
        case Handle_type::e_handle_type_rotate:          return show_rotate;
        default:                                         return false;
    }
}

void Trs_tool::Visualization::update_mesh_visibility(
    const std::shared_ptr<erhe::scene::Mesh>& mesh
)
{
    const auto active_handle = trs_tool.get_active_handle();
    const auto hover_handle  = trs_tool.get_hover_handle();
    const bool show_all      = is_visible && (active_handle == Handle::e_handle_none); // nothing is active, so show all handles
    const auto handle        = trs_tool.get_handle(mesh.get());
    const bool show          = get_handle_visibility(handle);
    const bool translate_x   = trs_tool.is_x_translate_active() && (handle == Handle::e_handle_translate_x);
    const bool translate_y   = trs_tool.is_y_translate_active() && (handle == Handle::e_handle_translate_y);
    const bool translate_z   = trs_tool.is_z_translate_active() && (handle == Handle::e_handle_translate_z);
    mesh->set_visibility_mask(
        show &&
        (
            !hide_inactive ||
            (active_handle == handle) ||
            (translate_x || translate_y || translate_z) ||
            show_all
        )
            ? (erhe::scene::Node_visibility::visible | erhe::scene::Node_visibility::tool | erhe::scene::Node_visibility::id)
            : erhe::scene::Node_visibility::tool
    );

    const Mode mode = (active_handle == handle) || (translate_x || translate_y || translate_z)
        ? Mode::Active
        : (hover_handle == handle)
            ? Mode::Hover
            : Mode::Normal;
    mesh->mesh_data.primitives.front().material = get_handle_material(handle, mode);
}

void Trs_tool::Visualization::update_visibility(
    const bool visible
)
{
    is_visible = visible;
    update_mesh_visibility(x_arrow_cylinder_mesh);
    update_mesh_visibility(x_arrow_neg_cone_mesh);
    update_mesh_visibility(x_arrow_pos_cone_mesh);
    update_mesh_visibility(y_arrow_cylinder_mesh);
    update_mesh_visibility(y_arrow_neg_cone_mesh);
    update_mesh_visibility(y_arrow_pos_cone_mesh);
    update_mesh_visibility(z_arrow_cylinder_mesh);
    update_mesh_visibility(z_arrow_neg_cone_mesh);
    update_mesh_visibility(z_arrow_pos_cone_mesh);
    update_mesh_visibility(xy_box_mesh          );
    update_mesh_visibility(xz_box_mesh          );
    update_mesh_visibility(yz_box_mesh          );
    update_mesh_visibility(x_rotate_ring_mesh   );
    update_mesh_visibility(y_rotate_ring_mesh   );
    update_mesh_visibility(z_rotate_ring_mesh   );
}

auto Trs_tool::Visualization::make_mesh(
    const std::string_view                            name,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const Part&                                       part
) -> std::shared_ptr<erhe::scene::Mesh>
{
    ERHE_VERIFY(tool_scene_root != nullptr);

    auto mesh = std::make_shared<erhe::scene::Mesh>(
        name,
        erhe::primitive::Primitive{
            .material              = material,
            .gl_primitive_geometry = part.primitive_geometry,
            .rt_primitive_geometry = part.raytrace_primitive
                ? part.raytrace_primitive->primitive_geometry
                : erhe::primitive::Primitive_geometry{}
        }
    );
    mesh->set_visibility_mask(erhe::scene::Node_visibility::tool);
    auto* parent = tool_node.get();
    if (parent != nullptr)
    {
        parent->attach(mesh);
    }

    const auto& layers     = tool_scene_root->layers();
    auto*       tool_layer = layers.tool();
    tool_scene_root->add(mesh, tool_layer);
    if (part.raytrace_primitive)
    {
        auto node_raytrace = std::make_shared<Node_raytrace>(
            part.geometry,
            part.raytrace_primitive
        );
        mesh->attach(node_raytrace);
        add_to_raytrace_scene(
            tool_scene_root->raytrace_scene(),
            node_raytrace
        );
    }
    return mesh;
}

namespace {
    constexpr float arrow_cylinder_length    = 2.5f;
    constexpr float arrow_cylinder_radius    = 0.08f;
    constexpr float arrow_cone_length        = 1.0f;
    constexpr float arrow_cone_radius        = 0.35f;
    constexpr float box_half_thickness       = 0.1f;
    constexpr float box_length               = 1.0f;
    constexpr float rotate_ring_major_radius = 4.0f;
    constexpr float rotate_ring_minor_radius = 0.1f;

    constexpr float arrow_tip = arrow_cylinder_length + arrow_cone_length;
}

auto Trs_tool::Visualization::make_arrow_cylinder(
    Mesh_memory& mesh_memory
) -> Part
{
    const auto geometry_shared = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_cylinder(
            -arrow_cylinder_length,
            arrow_cylinder_length,
            arrow_cylinder_radius,
            true,
            true,
            32,
            4
        )
    );

    return Part{
        .geometry           = geometry_shared,
        .primitive_geometry = erhe::primitive::make_primitive(*geometry_shared.get(), mesh_memory.build_info),
        .raytrace_primitive = std::make_shared<Raytrace_primitive>(geometry_shared)
    };
}

auto Trs_tool::Visualization::make_arrow_cone(
    Mesh_memory& mesh_memory
) -> Part
{
    const auto geometry_shared = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_cone(
            arrow_cylinder_length,
            arrow_tip,
            arrow_cone_radius,
            true,
            32,
            4
        )
    );

    return Part{
        .geometry           = geometry_shared,
        .primitive_geometry = make_primitive(*geometry_shared.get(), mesh_memory.build_info),
        .raytrace_primitive = std::make_shared<Raytrace_primitive>(geometry_shared)
    };
}

auto Trs_tool::Visualization::make_box(
    Mesh_memory& mesh_memory
) -> Part
{
    const auto geometry_shared = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_box(
            -box_length,
            box_length,
            -box_length,
            box_length,
            -box_half_thickness,
            box_half_thickness
        )
    );

    return Part{
        .geometry           = geometry_shared,
        .primitive_geometry = make_primitive(*geometry_shared.get(), mesh_memory.build_info),
        .raytrace_primitive = std::make_shared<Raytrace_primitive>(geometry_shared)
    };
}

auto Trs_tool::Visualization::make_rotate_ring(
    Mesh_memory& mesh_memory
) -> Part
{
    const auto geometry_shared = std::make_shared<erhe::geometry::Geometry>(
        erhe::geometry::shapes::make_torus(
            rotate_ring_major_radius,
            rotate_ring_minor_radius,
            80,
            32
        )
    );

    return Part{
        .geometry           = geometry_shared,
        .primitive_geometry = erhe::primitive::make_primitive(*geometry_shared.get(), mesh_memory.build_info),
        .raytrace_primitive = std::make_shared<Raytrace_primitive>(geometry_shared)
    };
}

auto Trs_tool::Visualization::get_handle_material(
    const Handle handle,
    const Mode   mode
) -> std::shared_ptr<erhe::primitive::Material>
{
    switch (handle)
    {
        case Handle::e_handle_translate_x : return (mode == Mode::Active) ? x_active_material : (mode == Mode::Hover) ? x_hover_material : x_material;
        case Handle::e_handle_translate_y : return (mode == Mode::Active) ? y_active_material : (mode == Mode::Hover) ? y_hover_material : y_material;
        case Handle::e_handle_translate_z : return (mode == Mode::Active) ? z_active_material : (mode == Mode::Hover) ? z_hover_material : z_material;
        case Handle::e_handle_translate_xy: return (mode == Mode::Active) ? z_active_material : (mode == Mode::Hover) ? z_hover_material : z_material;
        case Handle::e_handle_translate_xz: return (mode == Mode::Active) ? y_active_material : (mode == Mode::Hover) ? y_hover_material : y_material;
        case Handle::e_handle_translate_yz: return (mode == Mode::Active) ? x_active_material : (mode == Mode::Hover) ? x_hover_material : x_material;
        case Handle::e_handle_rotate_x    : return (mode == Mode::Active) ? x_active_material : (mode == Mode::Hover) ? x_hover_material : x_material;
        case Handle::e_handle_rotate_y    : return (mode == Mode::Active) ? y_active_material : (mode == Mode::Hover) ? y_hover_material : y_material;
        case Handle::e_handle_rotate_z    : return (mode == Mode::Active) ? z_active_material : (mode == Mode::Hover) ? z_hover_material : z_material;
        default: return {};
    }
}

[[nodiscard]] auto Trs_tool::Visualization::make_material(
    const char*      name,
    const glm::vec3& color,
    const Mode       mode
) -> std::shared_ptr<erhe::primitive::Material>
{
    if (mode == Mode::Active)
    {
        return material_library->make_material(name, glm::vec3(1.0f, 0.7f, 0.1f));
    }
    else if (mode == Mode::Hover)
    {
        return material_library->make_material(name, 2.0f * color);
    }
    else
    {
        return material_library->make_material(name, color);
    }
}

void Trs_tool::Visualization::initialize(
    const erhe::application::Configuration& configuration,
    Mesh_memory&                            mesh_memory,
    Scene_root*                             in_tool_scene_root
)
{
    ERHE_PROFILE_FUNCTION
    ERHE_VERIFY(in_tool_scene_root != nullptr);

    this->tool_scene_root = in_tool_scene_root;
    tool_node = std::make_shared<erhe::scene::Node>("Trs");
    //tool_scene_root->scene().add_node(tool_node);

    this->scale          = configuration.trs_tool.scale;
    this->show_translate = configuration.trs_tool.show_translate;
    this->show_rotate    = configuration.trs_tool.show_rotate;

    material_library = tool_scene_root->material_library();

    x_material        = make_material("x",        vec3{1.00f, 0.00f, 0.0f}, Mode::Normal);
    y_material        = make_material("y",        vec3{0.23f, 1.00f, 0.0f}, Mode::Normal);
    z_material        = make_material("z",        vec3{0.00f, 0.23f, 1.0f}, Mode::Normal);
    x_hover_material  = make_material("x hover",  vec3{1.00f, 0.00f, 0.0f}, Mode::Hover);
    y_hover_material  = make_material("y hover",  vec3{0.23f, 1.00f, 0.0f}, Mode::Hover);
    z_hover_material  = make_material("z hover",  vec3{0.00f, 0.23f, 1.0f}, Mode::Hover);
    x_active_material = make_material("x active", vec3{1.00f, 0.00f, 0.0f}, Mode::Active);
    y_active_material = make_material("y active", vec3{0.23f, 1.00f, 0.0f}, Mode::Active);
    z_active_material = make_material("z active", vec3{0.00f, 0.23f, 1.0f}, Mode::Active);

    x_material->visible = false;
    y_material->visible = false;
    z_material->visible = false;

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;

    const auto arrow_cylinder = make_arrow_cylinder(mesh_memory);
    const auto arrow_cone     = make_arrow_cone    (mesh_memory);
    const auto box            = make_box           (mesh_memory);
    const auto rotate_ring    = make_rotate_ring   (mesh_memory);

    x_arrow_cylinder_mesh  = make_mesh("X arrow cylinder", x_material, arrow_cylinder);
    x_arrow_neg_cone_mesh  = make_mesh("X arrow cone",     x_material, arrow_cone    );
    x_arrow_pos_cone_mesh  = make_mesh("X arrow cone",     x_material, arrow_cone    );
    y_arrow_cylinder_mesh  = make_mesh("Y arrow cylinder", y_material, arrow_cylinder);
    y_arrow_neg_cone_mesh  = make_mesh("Y arrow cone",     y_material, arrow_cone    );
    y_arrow_pos_cone_mesh  = make_mesh("Y arrow cone",     y_material, arrow_cone    );
    z_arrow_cylinder_mesh  = make_mesh("Z arrow cylinder", z_material, arrow_cylinder);
    z_arrow_neg_cone_mesh  = make_mesh("Z arrow cone",     z_material, arrow_cone    );
    z_arrow_pos_cone_mesh  = make_mesh("Z arrow cone",     z_material, arrow_cone    );
    xy_box_mesh            = make_mesh("XY box",           z_material, box           );
    xz_box_mesh            = make_mesh("XZ box",           y_material, box           );
    yz_box_mesh            = make_mesh("YZ box",           x_material, box           );
    x_rotate_ring_mesh     = make_mesh("X rotate ring",    x_material, rotate_ring   );
    y_rotate_ring_mesh     = make_mesh("Y rotate ring",    y_material, rotate_ring   );
    z_rotate_ring_mesh     = make_mesh("Z rotate ring",    z_material, rotate_ring   );

    using erhe::scene::Transform;
    const auto rotate_z_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_z_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_x_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, vec3{1.0f, 0.0f, 0.0f});
    const auto rotate_y_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_pos_180 = Transform::create_rotation(-glm::pi<float>()       , vec3{0.0f, 1.0f, 0.0f});

    x_arrow_cylinder_mesh->set_parent_from_node(mat4{1});
    x_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_180);
    x_arrow_pos_cone_mesh->set_parent_from_node(mat4{1});

    y_arrow_cylinder_mesh->set_parent_from_node(rotate_z_pos_90);
    y_arrow_neg_cone_mesh->set_parent_from_node(rotate_z_neg_90);
    y_arrow_pos_cone_mesh->set_parent_from_node(rotate_z_pos_90);
    z_arrow_cylinder_mesh->set_parent_from_node(rotate_y_neg_90);
    z_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_90);
    z_arrow_pos_cone_mesh->set_parent_from_node(rotate_y_neg_90);

    xy_box_mesh          ->set_parent_from_node(mat4{1});
    xz_box_mesh          ->set_parent_from_node(rotate_x_pos_90);
    yz_box_mesh          ->set_parent_from_node(rotate_y_neg_90);

    y_rotate_ring_mesh->set_parent_from_node(mat4{1});
    x_rotate_ring_mesh->set_parent_from_node(rotate_z_pos_90);
    z_rotate_ring_mesh->set_parent_from_node(rotate_x_pos_90);

    update_visibility(false);
}

void Trs_tool::set_local(const bool local)
{
    m_local = local;
    m_visualization.local = local;
}

void Trs_tool::viewport_toolbar()
{
    const auto& icon_rasterication = m_icon_set->get_small_rasterization();

    ImGui::SameLine();
    const auto local_pressed = erhe::application::make_button(
        "L",
        m_local
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal
    );
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Transform in Local space");
    }
    if (local_pressed)
    {
        set_local(true);
    }

    ImGui::SameLine();
    const auto global_pressed = erhe::application::make_button(
        "W",
        (!m_local)
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal
    );
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Transform in World space");
    }
    if (global_pressed)
    {
        set_local(false);
    }

    ImGui::SameLine();
    {
        const auto mode = m_visualization.show_translate
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal;

        erhe::application::begin_button_style(mode);
        const bool translate_pressed = icon_rasterication.icon_button(
            m_icon_set->icons.move,
            -1,
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            false
        );
        erhe::application::end_button_style(mode);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                m_visualization.show_translate
                    ? "Hide Translate Tool"
                    : "Show Translate Tool"
            );
        }
        if (translate_pressed)
        {
            m_visualization.show_translate = !m_visualization.show_translate;
            update_visibility();
        }
    }

    ImGui::SameLine();
    {
        const auto mode = m_visualization.show_rotate
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal;
        erhe::application::begin_button_style(mode);
        const bool rotate_pressed = icon_rasterication.icon_button(
            m_icon_set->icons.rotate,
            -1,
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            false
        );
        erhe::application::end_button_style(mode);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                m_visualization.show_rotate
                    ? "Hide Rotate Tool"
                    : "Show Rotate Tool"
            );
        }
        if (rotate_pressed)
        {
            m_visualization.show_rotate = !m_visualization.show_rotate;
            update_visibility();
        }
    }
}

void Trs_tool::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const bool   show_translate = m_visualization.show_translate;
    const bool   show_rotate    = m_visualization.show_rotate;
    const ImVec2 button_size{ImGui::GetContentRegionAvail().x, 0.0f};

    ImGui::Text("Hover handle: %s", c_str(m_hover_handle));
    ImGui::Text("Active handle: %s", c_str(m_active_handle));

    if (
        erhe::application::make_button(
            "Local",
            (m_local)
                ? erhe::application::Item_mode::active
                : erhe::application::Item_mode::normal,
            button_size
        )
    )
    {
        set_local(true);
    }
    if (
        erhe::application::make_button(
            "Global",
            (!m_local)
                ? erhe::application::Item_mode::active
                : erhe::application::Item_mode::normal,
            button_size
        )
    )
    {
        set_local(false);
    }

    ImGui::SliderFloat("Scale", &m_visualization.scale, 1.0f, 10.0f);

    {
        ImGui::Checkbox("Translate Tool",        &m_visualization.show_translate);
        ImGui::Checkbox("Translate Snap Enable", &m_translate_snap_enable);
        const float translate_snap_values[] = {  0.001f,  0.01f,  0.1f,  0.2f,  0.25f,  0.5f,  1.0f,  2.0f,  5.0f,  10.0f,  100.0f };
        const char* translate_snap_items [] = { "0.001", "0.01", "0.1", "0.2", "0.25", "0.5", "1.0", "2.0", "5.0", "10.0", "100.0" };
        erhe::application::make_combo(
            "Translate Snap",
            m_translate_snap_index,
            translate_snap_items,
            IM_ARRAYSIZE(translate_snap_items)
        );
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
        erhe::application::make_combo(
            "Rotate Snap",
            m_rotate_snap_index,
            rotate_snap_items,
            IM_ARRAYSIZE(rotate_snap_items)
        );
        if (
            (m_rotate_snap_index >= 0) &&
            (m_rotate_snap_index < IM_ARRAYSIZE(rotate_snap_values))
        )
        {
            m_rotate_snap = rotate_snap_values[m_rotate_snap_index];
        }
    }

    ImGui::Separator();
    ImGui::Checkbox ("Hide Inactive", &m_visualization.hide_inactive);

    if (
        (show_translate != m_visualization.show_translate) ||
        (show_rotate    != m_visualization.show_rotate   )
    )
    {
        update_visibility();
    }
#endif
}

void Trs_tool::tool_hover(Scene_view* scene_view)
{
    if (scene_view == nullptr)
    {
        m_hover_handle = Handle::e_handle_none;
        return;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || !tool.mesh)
    {
        if (m_hover_handle != Handle::e_handle_none)
        {
            m_hover_handle = Handle::e_handle_none;
            update_visibility();
        }
        return;
    }

    const auto new_handle = get_handle(tool.mesh.get());
    if (m_hover_handle == new_handle)
    {
        return;
    }
    m_hover_handle = get_handle(tool.mesh.get());
    update_visibility();
}

auto Trs_tool::on_drag(erhe::application::Command_context& context) -> bool
{
    auto* input_context = context.get_input_context();
    if (input_context == nullptr)
    {
        log_trs_tool->trace("TRS no input context");
        end_drag(context);
        return false;
    }
    const int type = input_context->get_type();
    Viewport_window* viewport_window = (type == Input_context_type_viewport_window) ? reinterpret_cast<Viewport_window*>(input_context) : nullptr;
    Scene_view*      scene_view      = reinterpret_cast<Scene_view*>(input_context);

    auto handle_type = get_handle_type(m_active_handle);
    switch (handle_type)
    {
        //using enum Handle_type;
        case Handle_type::e_handle_type_translate_axis:
        {
            if (type == Input_context_type_viewport_window)
            {
                //log_trs_tool->trace("update_axis_translate_2d(viewport_window);");
                update_axis_translate_2d(viewport_window);
            }
            else if (type == Input_context_type_headset_view)
            {
                //log_trs_tool->trace("update_axis_translate_3d(scene_view);");
                update_axis_translate_3d(scene_view);
            }
            return true;
        }

        case Handle_type::e_handle_type_translate_plane:
        {
            if (type == Input_context_type_viewport_window)
            {
                //log_trs_tool->trace("update_plane_translate_2d(viewport_window);");
                update_plane_translate_2d(viewport_window);
            }
            else if (type == Input_context_type_headset_view)
            {
                //log_trs_tool->trace("update_plane_translate_3d(scene_view);");
                update_plane_translate_3d(scene_view);
            }
            return true;
        }

        case Handle_type::e_handle_type_rotate:
        {
            update_rotate(scene_view);
            return true;
        }

        case Handle_type::e_handle_type_none:
        default:
        {
            return false;
        }
    }
}

auto Trs_tool::on_drag_ready(erhe::application::Command_context& context) -> bool
{
    static_cast<void>(context);

    log_trs_tool->trace("TRS on_drag_ready");

    //const auto viewport_window = m_viewport_windows->hover_window();
    //if (!viewport_window)
    //{
    //    return false;
    //}
    //

    m_active_handle = m_hover_handle; //get_handle(tool.mesh.get());

    Scene_view* scene_view = reinterpret_cast<Scene_view*>(context.get_input_context());
    if (scene_view == nullptr)
    {
        //log_trs_tool->trace("drag not possible - scene_view == nullptr");
        return false;
    }

    const auto& tool = scene_view->get_hover(Hover_entry::tool_slot);
    if (!tool.valid || !tool.mesh)
    {
        //log_trs_tool->trace("drag not possible - !tool.valid || !tool.mesh");
        return false;
    }

    if (
        (m_active_handle == Handle::e_handle_none) ||
        (root() == nullptr) ////||
        ////!viewport_window->position_in_viewport().has_value()
    )
    {
        //log_trs_tool->trace("drag not possible - no handle, or no root");
        return false;
    }

    const auto camera = scene_view->get_camera();
    if (!camera)
    {
        //log_trs_tool->trace("drag not possible - no camera");
        return false;
    }

    m_drag.initial_position_in_world = tool.position.value();
    m_drag.initial_world_from_local  = root()->world_from_node();
    m_drag.initial_local_from_world  = root()->node_from_world();
    m_drag.initial_distance          = glm::distance(
        glm::dvec3{camera->position_in_world()},
        tool.position.value()
    );
    if (m_target_node)
    {
        m_drag.initial_parent_from_node_transform = m_target_node->parent_from_node_transform();
    }

    // For rotation
    if (is_rotate_active())
    {
        const bool  world        = !m_local;
        const dvec3 n            = get_plane_normal(world);
        const dvec3 side         = get_plane_side  (world);
        const dvec3 center       = root()->position_in_world();
        const auto  intersection = project_pointer_to_plane(scene_view, n, center);
        if (intersection.has_value())
        {
            const dvec3 direction = normalize(intersection.value() - center);
            m_rotation = Rotation_context{
                .normal               = n,
                .reference_direction  = direction,
                .center_of_rotation   = center,
                .start_rotation_angle = erhe::toolkit::angle_of_rotation<double>(direction, n, side),
                .world_from_node      = m_target_node->world_from_node_transform()
            };
        }
        else
        {
            return false;
        }
    }

    //Tool::set_ready();
    update_visibility();

    if (m_node_physics)
    {
        auto* const rigid_body = m_node_physics->rigid_body();
        if (rigid_body != nullptr)
        {
            log_trs_tool->trace("m_node_physics and rigid_body != nullptr -> rigid_body->begin_move()");
            rigid_body->begin_move();
        }
    }

    log_trs_tool->trace("drag has been activated");
    return true;
}

void Trs_tool::end_drag(erhe::application::Command_context& context)
{
    log_trs_tool->trace("ending drag");

    static_cast<void>(context);

    m_active_handle                  = Handle::e_handle_none;
    m_drag.initial_position_in_world = dvec3{0.0};
    m_drag.initial_world_from_local  = dmat4{1};
    m_drag.initial_distance          = 0.0;
    update_visibility();

    if (m_touched && m_target_node)
    {
        log_trs_tool->trace("m_touched && m_target_node -> creating transform operation");

        m_operation_stack->push(
            std::make_shared<Node_transform_operation>(
                Node_transform_operation::Parameters{
                    .node                    = m_target_node,
                    .parent_from_node_before = m_drag.initial_parent_from_node_transform,
                    .parent_from_node_after  = m_target_node->parent_from_node_transform()
                }
            )
        );
        m_touched = false;
    }

    if (m_node_physics)
    {
        log_trs_tool->trace("m_node_physics -> rigid_body->end_move()");

        auto* const rigid_body = m_node_physics->rigid_body();
        rigid_body->end_move();
    }

    log_trs_tool->trace("drag ended");
}

auto Trs_tool::get_axis_direction() const -> dvec3
{
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_translate_x:  return m_local ? m_drag.initial_world_from_local[0] : dvec3{1.0, 0.0, 0.0};
        case Handle::e_handle_translate_y:  return m_local ? m_drag.initial_world_from_local[1] : dvec3{0.0, 1.0, 0.0};
        case Handle::e_handle_translate_z:  return m_local ? m_drag.initial_world_from_local[2] : dvec3{0.0, 0.0, 1.0};
        case Handle::e_handle_translate_yz: return m_local ? m_drag.initial_world_from_local[0] : dvec3{1.0, 0.0, 0.0};
        case Handle::e_handle_translate_xz: return m_local ? m_drag.initial_world_from_local[1] : dvec3{0.0, 1.0, 0.0};
        case Handle::e_handle_translate_xy: return m_local ? m_drag.initial_world_from_local[2] : dvec3{0.0, 0.0, 1.0};
        case Handle::e_handle_rotate_x:     return m_local ? m_drag.initial_world_from_local[0] : dvec3{1.0, 0.0, 0.0};
        case Handle::e_handle_rotate_y:     return m_local ? m_drag.initial_world_from_local[1] : dvec3{0.0, 1.0, 0.0};
        case Handle::e_handle_rotate_z:     return m_local ? m_drag.initial_world_from_local[2] : dvec3{0.0, 0.0, 1.0};
        default:
        {
            ERHE_FATAL("bad axis");
            break;
        }
    }
}

void Trs_tool::update_axis_translate_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION

    ////const auto viewport_window = m_viewport_windows->hover_window();
    if (viewport_window == nullptr)
    {
        return;
    }
    const auto position_in_viewport_opt = viewport_window->get_position_in_viewport();
    if (!position_in_viewport_opt.has_value())
    {
        return;
    }

    const dvec3 drag_world_direction = get_axis_direction();
    const dvec3 P0        = m_drag.initial_position_in_world - drag_world_direction;
    const dvec3 P1        = m_drag.initial_position_in_world + drag_world_direction;
    const auto  ss_P0_opt = viewport_window->project_to_viewport(P0);
    const auto  ss_P1_opt = viewport_window->project_to_viewport(P1);
    if (
        !ss_P0_opt.has_value() ||
        !ss_P1_opt.has_value()
    )
    {
        return;
    }
    const dvec3 ss_P0      = ss_P0_opt.value();
    const dvec3 ss_P1      = ss_P1_opt.value();
    const auto  ss_closest = erhe::toolkit::closest_point<double>(
        dvec2{ss_P0},
        dvec2{ss_P1},
        dvec2{position_in_viewport_opt.value()}
    );

    if (ss_closest.has_value())
    {
        const auto R0_opt = viewport_window->unproject_to_world(dvec3{ss_closest.value(), 0.0f});
        const auto R1_opt = viewport_window->unproject_to_world(dvec3{ss_closest.value(), 1.0f});
        if (R0_opt.has_value() && R1_opt.has_value())
        {
            const auto R0 = R0_opt.value();
            const auto R1 = R1_opt.value();
            const auto closest_points_r = erhe::toolkit::closest_points<double>(P0, P1, R0, R1);
            if (closest_points_r.has_value())
            {
                update_axis_translate_final(closest_points_r.value().P);
            }
        }
    }
    else
    {
        const auto Q0_opt = viewport_window->position_in_world_viewport_depth(1.0);
        const auto Q1_opt = viewport_window->position_in_world_viewport_depth(0.0);
        if (Q0_opt.has_value() && Q1_opt.has_value())
        {
            const auto Q0 = Q0_opt.value();
            const auto Q1 = Q1_opt.value();
            const auto closest_points_q = erhe::toolkit::closest_points<double>(P0, P1, Q0, Q1);
            if (closest_points_q.has_value())
            {
                update_axis_translate_final(closest_points_q.value().P);
            }
        }
    }
}

void Trs_tool::update_axis_translate_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    if (scene_view == nullptr)
    {
        log_trs_tool->trace("scene_view == nullptr");
        return;
    }

    const dvec3 drag_world_direction = get_axis_direction();
    const dvec3 P0 = m_drag.initial_position_in_world - drag_world_direction;
    const dvec3 P1 = m_drag.initial_position_in_world + drag_world_direction;
    const auto Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (Q_origin_opt.has_value() && Q_direction_opt.has_value())
    {
        const auto Q0 = Q_origin_opt.value();
        const auto Q1 = Q0 + Q_direction_opt.value();
        const auto closest_points_q = erhe::toolkit::closest_points<double>(P0, P1, Q0, Q1);
        if (closest_points_q.has_value())
        {
            update_axis_translate_final(closest_points_q.value().P);
        }
        else
        {
            log_trs_tool->trace("!closest_points_q.has_value()");
        }
    }
    else
    {
        log_trs_tool->trace("! (Q_origin_opt.has_value() && Q_direction_opt.has_value())");
    }
}

void Trs_tool::update_axis_translate_final(const dvec3 drag_position_in_world)
{
    const dvec3 translation_vector  = drag_position_in_world - m_drag.initial_position_in_world;
    const dvec3 snapped_translation = snap_translate(translation_vector);
    const dmat4 translation         = erhe::toolkit::create_translation<double>(snapped_translation);
    const dmat4 world_from_node     = translation * m_drag.initial_world_from_local;

    set_node_world_transform(world_from_node);
    update_transforms();
    m_touched = true;
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

auto Trs_tool::snap_translate(const dvec3 in_translation) const -> dvec3
{
    if (!m_translate_snap_enable)
    {
        return in_translation;
    }

    const double in_x = in_translation.x;
    const double in_y = in_translation.y;
    const double in_z = in_translation.z;
    const double snap = m_translate_snap;
    const double x    = is_x_translate_active() ? std::floor((in_x + snap * 0.5) / snap) * snap : in_x;
    const double y    = is_y_translate_active() ? std::floor((in_y + snap * 0.5) / snap) * snap : in_y;
    const double z    = is_z_translate_active() ? std::floor((in_z + snap * 0.5) / snap) * snap : in_z;

    return dvec3{
        x,
        y,
        z
    };
}

auto Trs_tool::snap_rotate(const double angle_radians) const -> double
{
    if (!m_rotate_snap_enable)
    {
        return angle_radians;
    }

    const double snap = glm::radians<double>(m_rotate_snap);
    return std::floor((angle_radians + snap * 0.5) / snap) * snap;
}

auto Trs_tool::get_plane_normal(const bool world) const -> dvec3
{
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x:
        case Handle::e_handle_translate_yz:
        {
            return world
                ? dvec3{1.0, 0.0, 0.0}
                : m_drag.initial_world_from_local[0];
        }

        case Handle::e_handle_rotate_y:
        case Handle::e_handle_translate_xz:
        {
            return world
                ? dvec3{0.0, 1.0, 0.0}
                : m_drag.initial_world_from_local[1];
        }

        case Handle::e_handle_rotate_z:
        case Handle::e_handle_translate_xy:
        {
            return world
                ? dvec3{0.0, 0.0, 1.0}
                : m_drag.initial_world_from_local[2];
        }

        default:
        {
            ERHE_FATAL("bad handle for plane %04x", static_cast<unsigned int>(m_active_handle));
            break;
        }
    }
}

auto Trs_tool::get_plane_side(const bool world) const -> dvec3
{
    switch (m_active_handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x:
        case Handle::e_handle_translate_yz: return world ? dvec3{0.0, 1.0, 0.0} : m_drag.initial_world_from_local[1];
        case Handle::e_handle_rotate_y:
        case Handle::e_handle_translate_xz: return world ? dvec3{0.0, 0.0, 1.0} : m_drag.initial_world_from_local[2];
        case Handle::e_handle_rotate_z:
        case Handle::e_handle_translate_xy: return world ? dvec3{1.0, 0.0, 0.0} : m_drag.initial_world_from_local[0];
        default:
            ERHE_FATAL("bad handle for plane %04x", static_cast<unsigned int>(m_active_handle));
            break;
    }
}

void Trs_tool::update_plane_translate_2d(Viewport_window* viewport_window)
{
    ERHE_PROFILE_FUNCTION

    if (viewport_window == nullptr)
    {
        return;
    }

    const dvec3 p0      = m_drag.initial_position_in_world;
    const dvec3 world_n = get_plane_normal(!m_local);
    const auto  Q0_opt  = viewport_window->position_in_world_viewport_depth(1.0);
    const auto  Q1_opt  = viewport_window->position_in_world_viewport_depth(0.0);
    if (
        !Q0_opt.has_value() ||
        !Q1_opt.has_value()
    )
    {
        return;
    }
    const dvec3 Q0 = Q0_opt.value();
    const dvec3 Q1 = Q1_opt.value();
    const dvec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<double>(world_n, p0, Q0, v);
    if (!intersection.has_value())
    {
        return;
    }

    const dvec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    const dvec3 translation_vector               = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    const dvec3 snapped_translation              = snap_translate(translation_vector);
    const dmat4 translation                      = erhe::toolkit::create_translation<double>(snapped_translation);
    const dmat4 world_from_node                  = translation * m_drag.initial_world_from_local;

    set_node_world_transform(world_from_node);
    update_transforms();
}

void Trs_tool::update_plane_translate_3d(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    if (scene_view == nullptr)
    {
        return;
    }

    const dvec3 p0      = m_drag.initial_position_in_world;
    const dvec3 world_n = get_plane_normal(!m_local);
    const auto  Q_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto  Q_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !Q_origin_opt.has_value() ||
        !Q_direction_opt.has_value()
    )
    {
        return;
    }
    const dvec3 Q0 = Q_origin_opt.value();
    const dvec3 Q1 = Q0 + Q_direction_opt.value();
    const dvec3 v  = normalize(Q1 - Q0);

    const auto intersection = erhe::toolkit::intersect_plane<double>(world_n, p0, Q0, v);
    if (!intersection.has_value())
    {
        return;
    }

    const dvec3 drag_point_new_position_in_world = Q0 + intersection.value() * v;
    const dvec3 translation_vector               = drag_point_new_position_in_world - m_drag.initial_position_in_world;
    const dvec3 snapped_translation              = snap_translate(translation_vector);
    const dmat4 translation                      = erhe::toolkit::create_translation<double>(snapped_translation);
    const dmat4 world_from_node                  = translation * m_drag.initial_world_from_local;

    set_node_world_transform(world_from_node);
    update_transforms();
}

auto Trs_tool::offset_plane_origo(const Handle handle, const dvec3 p) const -> dvec3
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x: return dvec3{ p.x, 0.0f, 0.0f};
        case Handle::e_handle_rotate_y: return dvec3{0.0f,  p.y, 0.0f};
        case Handle::e_handle_rotate_z: return dvec3{0.0f, 0.0f,  p.z};
        default:
            ERHE_FATAL("bad handle for rotate %04x", static_cast<unsigned int>(handle));
            break;
    }
}

auto Trs_tool::project_to_offset_plane(
    const Handle handle,
    const dvec3  P,
    const dvec3  Q
) const -> dvec3
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_rotate_x: return dvec3{P.x, Q.y, Q.z};
        case Handle::e_handle_rotate_y: return dvec3{Q.x, P.y, Q.z};
        case Handle::e_handle_rotate_z: return dvec3{Q.x, Q.y, P.z};
        default:
            ERHE_FATAL("bad handle for rotate %04x", static_cast<unsigned int>(handle));
            break;
    }
}

auto Trs_tool::project_pointer_to_plane(Scene_view* scene_view, const dvec3 n, const dvec3 p) -> std::optional<dvec3>
{
    if (scene_view == nullptr)
    {
        return {};
    }

    const auto origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto direction_opt = scene_view->get_control_ray_direction_in_world();
    if (
        !origin_opt.has_value() ||
        !direction_opt.has_value()
    )
    {
        return {};
    }

    const glm::dvec3 q0           = origin_opt.value();
    const glm::dvec3 v            = direction_opt.value();
    const auto       intersection = erhe::toolkit::intersect_plane<double>(n, p, q0, v);
    if (intersection.has_value())
    {
        return q0 + intersection.value() * v;
    }
    return {};
}

void Trs_tool::update_rotate(Scene_view* scene_view)
{
    ERHE_PROFILE_FUNCTION

    // log_trs_tool->trace("update_rotate()");

    if (root() == nullptr)
    {
        return;
    }

    //constexpr double c_parallel_threshold = 0.2;
    //const dvec3  V0      = dvec3{root()->position_in_world()} - dvec3{camera->position_in_world()};
    //const dvec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0});
    //const double v_dot_n = dot(V, m_rotation.normal);
    bool ready_to_rotate{false};
    //log_trs_tool->trace("R: {} @ {}", root()->name(), root()->position_in_world());
    //log_trs_tool->trace("C: {} @ {}", camera->name(), camera->position_in_world());
    //log_trs_tool->trace("V: {}", vec3{V});
    //log_trs_tool->trace("N: {}", vec3{m_rotation.normal});
    //log_trs_tool->trace("V.N = {}", v_dot_n);
    //if (std::abs(v_dot_n) > c_parallel_threshold) TODO
    {
        ready_to_rotate = update_rotate_circle_around(scene_view);
    }

    if (!ready_to_rotate)
    {
        ready_to_rotate = update_rotate_parallel(scene_view);
    }
    if (ready_to_rotate)
    {
        update_rotate_final();
    }
}

auto Trs_tool::update_rotate_circle_around(Scene_view* scene_view) -> bool
{
    m_rotation.intersection = project_pointer_to_plane(
        scene_view,
        m_rotation.normal,
        m_rotation.center_of_rotation
    );
    return m_rotation.intersection.has_value();
}

auto Trs_tool::update_rotate_parallel(Scene_view* scene_view) -> bool
{
    if (scene_view == nullptr)
    {
        return false;
    }
    const auto p_origin_opt    = scene_view->get_control_ray_origin_in_world();
    const auto p_direction_opt = scene_view->get_control_ray_direction_in_world();
    if (!p_origin_opt.has_value() || !p_direction_opt.has_value())
    {
        return false;
    }

    const auto p0        = p_origin_opt.value();
    const auto direction = p_direction_opt.value();
    const auto q0        = p0 + m_drag.initial_distance * direction;

    m_rotation.intersection = project_to_offset_plane(
        m_active_handle,
        m_rotation.center_of_rotation,
        q0
    );
    return true;
}

void Trs_tool::update_rotate_final()
{
    Expects(m_rotation.intersection.has_value());

    const dvec3  q_                     = normalize                                (m_rotation.intersection.value() - m_rotation.center_of_rotation);
    const double angle                  = erhe::toolkit::angle_of_rotation<double> (q_, m_rotation.normal, m_rotation.reference_direction);
    const double snapped_angle          = snap_rotate                              (angle);
    const dvec3  rotation_axis_in_world = get_axis_direction                       ();
    const dmat4  rotation               = erhe::toolkit::create_rotation<double>   (snapped_angle, rotation_axis_in_world);
    const dmat4  translate              = erhe::toolkit::create_translation<double>(dvec3{-m_rotation.center_of_rotation});
    const dmat4  untranslate            = erhe::toolkit::create_translation<double>(dvec3{ m_rotation.center_of_rotation});
    const dmat4  world_from_node        = untranslate * rotation * translate * m_drag.initial_world_from_local;

    m_rotation.current_angle = angle;

    set_node_world_transform(world_from_node);
    update_transforms();

    m_touched = true;
}

void Trs_tool::set_node_world_transform(const dmat4 world_from_node)
{
    if (root() == nullptr)
    {
        return;
    }
    const auto& root_parent = root()->parent().lock();
    const dmat4 parent_from_world = root_parent
        ? dmat4{root_parent->node_from_world()} * world_from_node
        : world_from_node;
    root()->set_parent_from_node(mat4{parent_from_world});
 }

void Trs_tool::update_for_view(Scene_view* scene_view)
{
    if (scene_view == nullptr)
    {
        return;
    }

    const auto camera = scene_view->get_camera();
    if (!camera)
    {
        return;
    }

    const vec3 view_position_in_world = vec3{camera->position_in_world()};

    m_visualization.update_scale(view_position_in_world);
    update_transforms();

    //if (root() == nullptr)
    //{
    //    return;
    //}
    //
    //const dvec3  V0      = dvec3{root()->position_in_world()} - dvec3{camera->position_in_world()};
    //const dvec3  V       = normalize(m_drag.initial_local_from_world * vec4{V0, 0.0});
    //const double v_dot_n = dot(V, m_rotation.normal);
    //->tail_log.trace("R: {} @ {}", root()->name(), root()->position_in_world());
    //->tail_log.trace("C: {} @ {}", camera->name(), camera->position_in_world());
    //->tail_log.trace("V: {}", vec3{V});
    //->tail_log.trace("N: {}", vec3{m_rotation.normal});
    //->tail_log.trace("V.N = {}", v_dot_n);
}

void Trs_tool::tool_render(
    const Render_context& context
)
{
    ERHE_PROFILE_FUNCTION

    if (
        (m_line_renderer_set == nullptr) ||
        (get_handle_type(m_active_handle) != Handle_type::e_handle_type_rotate) ||
        (context.camera == nullptr) ||
        (root() == nullptr)
    )
    {
        return;
    }

    auto& line_renderer = *m_line_renderer_set->hidden.at(2).get();

    const dvec3  p                 = m_rotation.center_of_rotation;
    const dvec3  n                 = m_rotation.normal;
    const dvec3  side1             = m_rotation.reference_direction;
    const dvec3  side2             = normalize(cross(n, side1));
    const dvec3  position_in_world = root()->position_in_world();
    const double distance          = length(position_in_world - dvec3{context.camera->position_in_world()});
    const double scale             = m_visualization.scale * distance / 100.0;
    const double r1                = scale * 6.0;

    constexpr uint32_t red       = 0xff0000ffu;
    constexpr uint32_t blue      = 0xffff0000u;
    constexpr uint32_t orange    = 0xcc0088ffu;

    {
        const int sector_count = m_rotate_snap_enable
            ? static_cast<int>(glm::two_pi<double>() / glm::radians(double{m_rotate_snap}))
            : 80;
        std::vector<vec3> positions;

        line_renderer.set_line_color(orange);
        line_renderer.set_thickness(-1.41f);
        for (int i = 0; i < sector_count + 1; ++i)
        {
            const double rel   = static_cast<double>(i) / static_cast<double>(sector_count);
            const double theta = rel * glm::two_pi<double>();
            const bool   first = (i == 0);
            const bool   major = (i % 10 == 0);
            const double r0    =
                first
                    ? 0.0
                    : major
                        ? 5.0 * scale
                        : 5.5 * scale;

            const dvec3 p0 =
                p +
                r0 * std::cos(theta) * side1 +
                r0 * std::sin(theta) * side2;

            const dvec3 p1 =
                p +
                r1 * std::cos(theta) * side1 +
                r1 * std::sin(theta) * side2;

            line_renderer.add_lines(
                {
                    {
                        p0,
                        p1
                    }
                }
            );
        }
    }

    // Circle (ring)
    {
        constexpr int sector_count = 200;
        std::vector<vec3> positions;
        for (int i = 0; i < sector_count + 1; ++i)
        {
            const double rel   = static_cast<double>(i) / static_cast<double>(sector_count);
            const double theta = rel * glm::two_pi<double>();
            positions.emplace_back(
                p +
                r1 * std::cos(theta) * side1 +
                r1 * std::sin(theta) * side2
            );
        }
        for (size_t i = 0, count = positions.size(); i < count; ++i)
        {
            const std::size_t next_i = (i + 1) % count;
            line_renderer.add_lines(
                {
                    {
                        positions[i],
                        positions[next_i]
                    }
                }
            );
        }
    }

    const double snapped_angle = snap_rotate(m_rotation.current_angle);
    const auto snapped =
        p +
        r1 * std::cos(snapped_angle) * side1 +
        r1 * std::sin(snapped_angle) * side2;

    line_renderer.add_lines(red,                             { { p, r1 * side1 } } );
    line_renderer.add_lines(blue,                            { { p, snapped    } } );
    line_renderer.add_lines(get_axis_color(m_active_handle), { { p - 10.0 * n, p + 10.0 * n } } );
}

auto Trs_tool::get_active_handle() const -> Handle
{
    return m_active_handle;
}

auto Trs_tool::get_hover_handle() const -> Handle
{
    return m_hover_handle;
}

auto Trs_tool::get_handle(erhe::scene::Mesh* mesh) const -> Trs_tool::Handle
{
    const auto i = m_handles.find(mesh);
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
        //using enum Handle;
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
        {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

auto Trs_tool::get_axis_color(const Handle handle) const -> uint32_t
{
    switch (handle)
    {
        //using enum Handle;
        case Handle::e_handle_translate_x: return 0xff0000ff;
        case Handle::e_handle_translate_y: return 0xff00ff00;
        case Handle::e_handle_translate_z: return 0xffff0000;
        case Handle::e_handle_rotate_x:    return 0xff0000ff;
        case Handle::e_handle_rotate_y:    return 0xff00ff00;
        case Handle::e_handle_rotate_z:    return 0xffff0000;
        case Handle::e_handle_none:
        default:
        {
            ERHE_FATAL("bad handle %04x", static_cast<unsigned int>(handle));
        }
    }
}

void Trs_tool::Visualization::update_transforms() //const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION

    if (root == nullptr)
    {
        return;
    }

    auto world_from_root_transform = local
        ? root->world_from_node_transform()
        : erhe::scene::Transform::create_translation(root->position_in_world());

    const mat4 scaling = erhe::toolkit::create_scale<float>(scale * view_distance / 100.0f);
    world_from_root_transform.catenate(scaling);

    tool_node->set_parent_from_node(world_from_root_transform);
}

void Trs_tool::update_transforms()
{
    if (root() == nullptr)
    {
        return;
    }
    auto* scene_root = reinterpret_cast<Scene_root*>(m_target_node->node_data.host);
    if (scene_root == nullptr)
    {
        log_trs_tool->error("Node '{}' has no scene root");
        return;
    }
    m_visualization.update_transforms();
}

void Trs_tool::on_enable_state_changed()
{
    log_trs_tool->info("on_enable_state_changed()");

    update_visibility();
}

void Trs_tool::update_visibility()
{
    m_visualization.update_visibility(m_target_node != nullptr);
    update_transforms();
}

auto Trs_tool::root() -> erhe::scene::Node*
{
    return m_visualization.root;
}

} // namespace editor
