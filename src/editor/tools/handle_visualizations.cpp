#include "tools/handle_visualizations.hpp"
#include "tools/trs_tool.hpp"
#include "tools/tools.hpp"

#include "editor_scenes.hpp"
#include "renderers/mesh_memory.hpp"
#include "graphics/icon_set.hpp"
#include "scene/material_library.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "tools/tools.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
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

[[nodiscard]] auto c_str(const Handle handle) -> const char*
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

Handle_visualizations::Handle_visualizations(Trs_tool& trs_tool)
    : m_trs_tool{trs_tool}
{
}

void Handle_visualizations::update_scale(
    const glm::vec3 view_position_in_world
)
{
    const auto root = m_root.lock();
    if (!root)
    {
        return;
    }

    const glm::vec3 position_in_world = root->position_in_world();
    m_view_distance = glm::length(position_in_world - glm::vec3{view_position_in_world});
}

auto Handle_visualizations::get_handle_visibility(const Handle handle) const -> bool
{
    switch (m_trs_tool.get_handle_type(handle))
    {
        case Handle_type::e_handle_type_translate_axis:  return m_show_translate;
        case Handle_type::e_handle_type_translate_plane: return m_show_translate;
        case Handle_type::e_handle_type_rotate:          return m_show_rotate;
        default:                                         return false;
    }
}

void Handle_visualizations::update_mesh_visibility(
    const std::shared_ptr<erhe::scene::Mesh>& mesh
)
{
    const auto active_handle = m_trs_tool.get_active_handle();
    const auto hover_handle  = m_trs_tool.get_hover_handle();
    const bool show_all      = m_is_visible && (active_handle == Handle::e_handle_none); // nothing is active, so show all handles
    const auto handle        = m_trs_tool.get_handle(mesh.get());
    const bool show          = get_handle_visibility(handle);
    const bool translate_x   = m_trs_tool.is_x_translate_active() && (handle == Handle::e_handle_translate_x);
    const bool translate_y   = m_trs_tool.is_y_translate_active() && (handle == Handle::e_handle_translate_y);
    const bool translate_z   = m_trs_tool.is_z_translate_active() && (handle == Handle::e_handle_translate_z);
    mesh->set_visibility_mask(
        show &&
        (
            !m_hide_inactive ||
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

void Handle_visualizations::update_visibility(
    const bool visible
)
{
    m_is_visible = visible;
    update_mesh_visibility(m_x_arrow_cylinder_mesh);
    update_mesh_visibility(m_x_arrow_neg_cone_mesh);
    update_mesh_visibility(m_x_arrow_pos_cone_mesh);
    update_mesh_visibility(m_y_arrow_cylinder_mesh);
    update_mesh_visibility(m_y_arrow_neg_cone_mesh);
    update_mesh_visibility(m_y_arrow_pos_cone_mesh);
    update_mesh_visibility(m_z_arrow_cylinder_mesh);
    update_mesh_visibility(m_z_arrow_neg_cone_mesh);
    update_mesh_visibility(m_z_arrow_pos_cone_mesh);
    update_mesh_visibility(m_xy_box_mesh          );
    update_mesh_visibility(m_xz_box_mesh          );
    update_mesh_visibility(m_yz_box_mesh          );
    update_mesh_visibility(m_x_rotate_ring_mesh   );
    update_mesh_visibility(m_y_rotate_ring_mesh   );
    update_mesh_visibility(m_z_rotate_ring_mesh   );
}

auto Handle_visualizations::make_mesh(
    const std::string_view                            name,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const Part&                                       part
) -> std::shared_ptr<erhe::scene::Mesh>
{
    ERHE_PROFILE_FUNCTION

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

    if (part.raytrace_primitive)
    {
        auto node_raytrace = std::make_shared<Node_raytrace>(
            part.geometry,
            part.raytrace_primitive
        );
        mesh->attach(node_raytrace);
    }

    const auto scene_root    = m_trs_tool.get_tool_scene_root();
    const auto tool_layer_id = scene_root->layers().tool()->id.get_id();
    mesh->mesh_data.layer_id = tool_layer_id;
    mesh->set_parent(m_tool_node);
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

auto Handle_visualizations::make_arrow_cylinder(
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

auto Handle_visualizations::make_arrow_cone(
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

auto Handle_visualizations::make_box(
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

auto Handle_visualizations::make_rotate_ring(
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

auto Handle_visualizations::get_mode_material(
    const Mode                                        mode,
    const std::shared_ptr<erhe::primitive::Material>& active,
    const std::shared_ptr<erhe::primitive::Material>& hover,
    const std::shared_ptr<erhe::primitive::Material>& normal
) -> std::shared_ptr<erhe::primitive::Material>
{
    switch (mode)
    {
        case Mode::Normal: return normal;
        case Mode::Active: return active;
        case Mode::Hover:  return hover;
        default:           return {};
    }
}

auto Handle_visualizations::get_handle_material(
    const Handle handle,
    const Mode   mode
) -> std::shared_ptr<erhe::primitive::Material>
{
    switch (handle)
    {
        case Handle::e_handle_translate_x : return get_mode_material(mode, m_x_active_material, m_x_hover_material, m_x_material);
        case Handle::e_handle_translate_y : return get_mode_material(mode, m_y_active_material, m_y_hover_material, m_y_material);
        case Handle::e_handle_translate_z : return get_mode_material(mode, m_z_active_material, m_z_hover_material, m_z_material);
        case Handle::e_handle_translate_xy: return get_mode_material(mode, m_z_active_material, m_z_hover_material, m_z_material);
        case Handle::e_handle_translate_xz: return get_mode_material(mode, m_y_active_material, m_y_hover_material, m_y_material);
        case Handle::e_handle_translate_yz: return get_mode_material(mode, m_x_active_material, m_x_hover_material, m_x_material);
        case Handle::e_handle_rotate_x    : return get_mode_material(mode, m_x_active_material, m_x_hover_material, m_x_material);
        case Handle::e_handle_rotate_y    : return get_mode_material(mode, m_y_active_material, m_y_hover_material, m_y_material);
        case Handle::e_handle_rotate_z    : return get_mode_material(mode, m_z_active_material, m_z_hover_material, m_z_material);
        default: return {};
    }
}

[[nodiscard]] auto Handle_visualizations::make_material(
    const char*      name,
    const glm::vec3& color,
    const Mode       mode
) -> std::shared_ptr<erhe::primitive::Material>
{
    const auto material_library = m_trs_tool.get_tool_scene_root()->material_library();
    switch (mode)
    {
        case Mode::Normal: return material_library->make_material(name, color);
        case Mode::Active: return material_library->make_material(name, glm::vec3(1.0f, 0.7f, 0.1f));
        case Mode::Hover:  return material_library->make_material(name, 2.0f * color);
        default:           return {};
    }
}

void Handle_visualizations::initialize(
    const erhe::application::Configuration& configuration,
    Mesh_memory&                            mesh_memory
)
{
    ERHE_PROFILE_FUNCTION

    m_tool_node = std::make_shared<erhe::scene::Node>("Trs");
    const auto scene_root = m_trs_tool.get_tool_scene_root();
    m_tool_node->set_parent(scene_root->get_scene()->root_node);

    m_scale          = configuration.trs_tool.scale;
    m_show_translate = configuration.trs_tool.show_translate;
    m_show_rotate    = configuration.trs_tool.show_rotate;

    m_x_material        = make_material("x",        glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Normal);
    m_y_material        = make_material("y",        glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Normal);
    m_z_material        = make_material("z",        glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Normal);
    m_x_hover_material  = make_material("x hover",  glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Hover);
    m_y_hover_material  = make_material("y hover",  glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Hover);
    m_z_hover_material  = make_material("z hover",  glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Hover);
    m_x_active_material = make_material("x active", glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Active);
    m_y_active_material = make_material("y active", glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Active);
    m_z_active_material = make_material("z active", glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Active);

    m_x_material->visible = false;
    m_y_material->visible = false;
    m_z_material->visible = false;

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue;

    const auto arrow_cylinder = make_arrow_cylinder(mesh_memory);
    const auto arrow_cone     = make_arrow_cone    (mesh_memory);
    const auto box            = make_box           (mesh_memory);
    const auto rotate_ring    = make_rotate_ring   (mesh_memory);

    m_x_arrow_cylinder_mesh  = make_mesh("X arrow cylinder", m_x_material, arrow_cylinder);
    m_x_arrow_neg_cone_mesh  = make_mesh("X arrow cone",     m_x_material, arrow_cone    );
    m_x_arrow_pos_cone_mesh  = make_mesh("X arrow cone",     m_x_material, arrow_cone    );
    m_y_arrow_cylinder_mesh  = make_mesh("Y arrow cylinder", m_y_material, arrow_cylinder);
    m_y_arrow_neg_cone_mesh  = make_mesh("Y arrow cone",     m_y_material, arrow_cone    );
    m_y_arrow_pos_cone_mesh  = make_mesh("Y arrow cone",     m_y_material, arrow_cone    );
    m_z_arrow_cylinder_mesh  = make_mesh("Z arrow cylinder", m_z_material, arrow_cylinder);
    m_z_arrow_neg_cone_mesh  = make_mesh("Z arrow cone",     m_z_material, arrow_cone    );
    m_z_arrow_pos_cone_mesh  = make_mesh("Z arrow cone",     m_z_material, arrow_cone    );
    m_xy_box_mesh            = make_mesh("XY box",           m_z_material, box           );
    m_xz_box_mesh            = make_mesh("XZ box",           m_y_material, box           );
    m_yz_box_mesh            = make_mesh("YZ box",           m_x_material, box           );
    m_x_rotate_ring_mesh     = make_mesh("X rotate ring",    m_x_material, rotate_ring   );
    m_y_rotate_ring_mesh     = make_mesh("Y rotate ring",    m_y_material, rotate_ring   );
    m_z_rotate_ring_mesh     = make_mesh("Z rotate ring",    m_z_material, rotate_ring   );

    m_handles[m_x_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_x_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_x_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_x;
    m_handles[m_y_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_y_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_y_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_y;
    m_handles[m_z_arrow_cylinder_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_z_arrow_neg_cone_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_z_arrow_pos_cone_mesh.get()] = Handle::e_handle_translate_z;
    m_handles[m_xy_box_mesh          .get()] = Handle::e_handle_translate_xy;
    m_handles[m_xz_box_mesh          .get()] = Handle::e_handle_translate_xz;
    m_handles[m_yz_box_mesh          .get()] = Handle::e_handle_translate_yz;
    m_handles[m_x_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_x;
    m_handles[m_y_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_y;
    m_handles[m_z_rotate_ring_mesh   .get()] = Handle::e_handle_rotate_z;

    using erhe::scene::Transform;
    const auto rotate_z_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_z_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f});
    const auto rotate_x_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{1.0f, 0.0f, 0.0f});
    const auto rotate_y_pos_90  = Transform::create_rotation( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_neg_90  = Transform::create_rotation(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f});
    const auto rotate_y_pos_180 = Transform::create_rotation(-glm::pi<float>()       , glm::vec3{0.0f, 1.0f, 0.0f});

    m_x_arrow_cylinder_mesh->set_parent_from_node(glm::mat4{1.0f});
    m_x_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_180);
    m_x_arrow_pos_cone_mesh->set_parent_from_node(glm::mat4{1.0f});

    m_y_arrow_cylinder_mesh->set_parent_from_node(rotate_z_pos_90);
    m_y_arrow_neg_cone_mesh->set_parent_from_node(rotate_z_neg_90);
    m_y_arrow_pos_cone_mesh->set_parent_from_node(rotate_z_pos_90);
    m_z_arrow_cylinder_mesh->set_parent_from_node(rotate_y_neg_90);
    m_z_arrow_neg_cone_mesh->set_parent_from_node(rotate_y_pos_90);
    m_z_arrow_pos_cone_mesh->set_parent_from_node(rotate_y_neg_90);

    m_xy_box_mesh          ->set_parent_from_node(glm::mat4{1.0f});
    m_xz_box_mesh          ->set_parent_from_node(rotate_x_pos_90);
    m_yz_box_mesh          ->set_parent_from_node(rotate_y_neg_90);

    m_y_rotate_ring_mesh->set_parent_from_node(glm::mat4{1.0f});
    m_x_rotate_ring_mesh->set_parent_from_node(rotate_z_pos_90);
    m_z_rotate_ring_mesh->set_parent_from_node(rotate_x_pos_90);

    update_visibility(false);
}

auto Handle_visualizations::get_handle(erhe::scene::Mesh* mesh) const -> Handle
{
    const auto i = m_handles.find(mesh);
    if (i == m_handles.end())
    {
        return Handle::e_handle_none;
    }
    return i->second;
}

void Handle_visualizations::update_transforms() //const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION

    const auto root = m_root.lock();
    if (!root)
    {
        return;
    }

    auto world_from_root_transform = m_local
        ? root->world_from_node_transform()
        : erhe::scene::Transform::create_translation(root->position_in_world());

    const glm::mat4 scaling = erhe::toolkit::create_scale<float>(m_scale * m_view_distance / 100.0f);
    world_from_root_transform.catenate(scaling);

    m_tool_node->set_parent_from_node(world_from_root_transform);
}

void Handle_visualizations::set_target(
    const std::shared_ptr<erhe::scene::Node>& node
)
{
    m_root = node;
    if (!node)
    {
        return;
    }
}

void Handle_visualizations::set_translate(const bool enabled)
{
    m_show_translate = enabled;
}

void Handle_visualizations::set_rotate(const bool enabled)
{
    m_show_rotate = enabled;
}

void Handle_visualizations::set_local(const bool local)
{
    m_local = local;
}

auto Handle_visualizations::get_scale() const -> float
{
    return m_scale;
}

auto Handle_visualizations::get_translate() const -> bool
{
    return m_show_translate;
}

auto Handle_visualizations::get_rotate() const -> bool
{
    return m_show_rotate;
}

void Handle_visualizations::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const bool   show_translate = m_show_translate;
    const bool   show_rotate    = m_show_rotate;
    const ImVec2 button_size{ImGui::GetContentRegionAvail().x, 0.0f};

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

    ImGui::SliderFloat("Scale", &m_scale, 1.0f, 10.0f);

    ImGui::Checkbox("Translate Tool", &m_show_translate);
    ImGui::Checkbox("Rotate Tool",    &m_show_rotate);
    ImGui::Checkbox("Hide Inactive",  &m_hide_inactive);

    if (
        (show_translate != m_show_translate) ||
        (show_rotate    != m_show_rotate   )
    )
    {
        update_visibility(!m_root.expired());
    }
#endif
}

void Handle_visualizations::viewport_toolbar(const Icon_set& icon_set)
{
    const auto& icon_rasterication = icon_set.get_small_rasterization();

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
        const auto mode = m_show_translate
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal;

        erhe::application::begin_button_style(mode);
        const bool translate_pressed = icon_rasterication.icon_button(
            icon_set.icons.move,
            -1,
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            false
        );
        erhe::application::end_button_style(mode);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                m_show_translate
                    ? "Hide Translate Tool"
                    : "Show Translate Tool"
            );
        }
        if (translate_pressed)
        {
            m_show_translate = !m_show_translate;
            update_visibility(!m_root.expired());
        }
    }

    ImGui::SameLine();
    {
        const auto mode = m_show_rotate
            ? erhe::application::Item_mode::active
            : erhe::application::Item_mode::normal;
        erhe::application::begin_button_style(mode);
        const bool rotate_pressed = icon_rasterication.icon_button(
            icon_set.icons.rotate,
            -1,
            glm::vec4{0.0f, 0.0f, 0.0f, 0.0f},
            glm::vec4{1.0f, 1.0f, 1.0f, 1.0f},
            false
        );
        erhe::application::end_button_style(mode);
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(
                m_show_rotate
                    ? "Hide Rotate Tool"
                    : "Show Rotate Tool"
            );
        }
        if (rotate_pressed)
        {
            m_show_rotate = !m_show_rotate;
            update_visibility(!m_root.expired());
        }
    }
}

} // namespace editor
