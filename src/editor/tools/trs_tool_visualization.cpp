#include "tools/trs_tool.hpp"

#include "renderers/mesh_memory.hpp"
#include "scene/helpers.hpp"
#include "scene/material_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/geometry/shapes/box.hpp"
#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/shapes/torus.hpp"
#include "erhe/graphics/buffer.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/primitive/primitive_builder.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{


Trs_tool::Visualization::Visualization(Trs_tool& trs_tool)
    : trs_tool{trs_tool}
{
}

void Trs_tool::Visualization::update_scale(
    const glm::vec3 view_position_in_world
)
{
    const auto root = m_root.lock();
    if (!root)
    {
        return;
    }

    const glm::vec3 position_in_world = root->position_in_world();
    view_distance = glm::length(position_in_world - glm::vec3{view_position_in_world});
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
    ERHE_PROFILE_FUNCTION
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
    ERHE_PROFILE_FUNCTION

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
    ERHE_PROFILE_FUNCTION

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
    ERHE_PROFILE_FUNCTION

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
    ERHE_PROFILE_FUNCTION

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

    x_material        = make_material("x",        glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Normal);
    y_material        = make_material("y",        glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Normal);
    z_material        = make_material("z",        glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Normal);
    x_hover_material  = make_material("x hover",  glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Hover);
    y_hover_material  = make_material("y hover",  glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Hover);
    z_hover_material  = make_material("z hover",  glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Hover);
    x_active_material = make_material("x active", glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Active);
    y_active_material = make_material("y active", glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Active);
    z_active_material = make_material("z active", glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Active);

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
    const auto rotate_z_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_z_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_x_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{1.0f, 0.0f, 0.0f});
    const auto rotate_y_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_pos_180 = Transform::create_rotation(-glm::pi<float>()       , glm::vec3{0.0f, 1.0f, 0.0f});

    x_arrow_cylinder_mesh->set_parent_from_node(glm::mat4{1.0f});
    x_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_180);
    x_arrow_pos_cone_mesh->set_parent_from_node(glm::mat4{1.0f});

    y_arrow_cylinder_mesh->set_parent_from_node(rotate_z_pos_90);
    y_arrow_neg_cone_mesh->set_parent_from_node(rotate_z_neg_90);
    y_arrow_pos_cone_mesh->set_parent_from_node(rotate_z_pos_90);
    z_arrow_cylinder_mesh->set_parent_from_node(rotate_y_neg_90);
    z_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_90);
    z_arrow_pos_cone_mesh->set_parent_from_node(rotate_y_neg_90);

    xy_box_mesh          ->set_parent_from_node(glm::mat4{1.0f});
    xz_box_mesh          ->set_parent_from_node(rotate_x_pos_90);
    yz_box_mesh          ->set_parent_from_node(rotate_y_neg_90);

    y_rotate_ring_mesh->set_parent_from_node(glm::mat4{1.0f});
    x_rotate_ring_mesh->set_parent_from_node(rotate_z_pos_90);
    z_rotate_ring_mesh->set_parent_from_node(rotate_x_pos_90);

    update_visibility(false);
}

} // namespace editor
