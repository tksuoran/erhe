#include "transform/handle_visualizations.hpp"

#include "app_context.hpp"
#include "config/generated/editor_settings_config.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/scene_view.hpp"
#include "tools/tools.hpp"
#include "transform/handle_enums.hpp"
#include "transform/move_tool.hpp"
#include "transform/rotate_tool.hpp"
#include "transform/scale_tool.hpp"
#include "transform/transform_tool.hpp"

#include "erhe_imgui/imgui_helpers.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/shapes/box.hpp"
#include "erhe_geometry/shapes/cone.hpp"
#include "erhe_geometry/shapes/torus.hpp"
#include "erhe_graphics/buffer_transfer_queue.hpp"
#include "erhe_log/log_glm.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/gtc/quaternion.hpp>

namespace editor {

namespace {
    constexpr float arrow_cylinder_length              = 2.75f;
    constexpr float arrow_cylinder_radius_render       = 0.03f;
    constexpr float arrow_cylinder_radius_collision    = arrow_cylinder_radius_render * 3.0f;
    constexpr float arrow_cone_length_render           = 0.6f;
    constexpr float arrow_cone_length_collision        = arrow_cone_length_render * 2.0f;
    constexpr float arrow_cone_radius_render           = 0.15f;
    constexpr float arrow_cone_radius_collision        = arrow_cone_radius_render * 3.0f;
    constexpr float box_half_thickness_render          = 0.01f;
    constexpr float box_half_thickness_collision       = box_half_thickness_render * 2.0f;
    constexpr float box_length_render                  = 0.6f;
    constexpr float box_length_collision               = box_length_render * 1.3f;
    constexpr float rotate_ring_major_radius           = 4.0f;
    constexpr float rotate_ring_minor_radius_render    = 0.05f;
    constexpr float rotate_ring_minor_radius_collision = rotate_ring_minor_radius_render * 4.0f;

    constexpr float arrow_tip_render = arrow_cylinder_length + arrow_cone_length_render;
    constexpr float arrow_tip_collision = arrow_cylinder_length + arrow_cone_length_collision;

    constexpr float box_scale_cone_half_length_render    = 0.3f;
    constexpr float box_scale_cone_radius_render         = 0.2f;
    constexpr float box_scale_cone_half_length_collision = box_scale_cone_half_length_render * 1.5f;
    constexpr float box_scale_cone_radius_collision      = box_scale_cone_radius_render * 2.0f;

    // Uniform-scale center cube. Kept well inside the plane-scale thin boxes (which span
    // +/- box_length through the origin) so their outer regions stay pickable: rays through
    // the central +/- center_cube_half_length_collision area pick the cube, rays farther
    // out still reach the plane handles.
    constexpr float center_cube_half_length_render    = 0.25f;
    constexpr float center_cube_half_length_collision = center_cube_half_length_render * 1.3f;
}

auto Handle_visualizations::c_str(const Mode mode) -> const char*
{
    switch (mode) {
        case Mode::Normal: return "normal";
        case Mode::Hover : return "hover";
        case Mode::Active: return "active";
        default:           return "?";
    }
}

Handle_visualizations::Handle_visualizations(
    App_context&                       app_context,
    erhe::scene_renderer::Mesh_memory& mesh_memory,
    Tools&                             tools
)
    : m_context{app_context}
{
    ERHE_PROFILE_FUNCTION();

    m_tool_node = std::make_shared<erhe::scene::Node>("Trs");
    m_box_node  = std::make_shared<erhe::scene::Node>("Trs box");
    const auto scene_root = tools.get_tool_scene_root();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

    m_tool_node->set_parent(scene_root->get_hosted_scene()->get_root_node());
    m_box_node ->set_parent(scene_root->get_hosted_scene()->get_root_node());

    m_pos_x_material        = make_material(tools, "X+",        glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Normal);
    m_pos_y_material        = make_material(tools, "Y+",        glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Normal);
    m_pos_z_material        = make_material(tools, "Z+",        glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Normal);
    m_pos_x_hover_material  = make_material(tools, "X+ hover",  glm::vec3{2.00f, 0.00f, 0.0f}, Mode::Hover);
    m_pos_y_hover_material  = make_material(tools, "Y+ hover",  glm::vec3{0.23f, 2.00f, 0.0f}, Mode::Hover);
    m_pos_z_hover_material  = make_material(tools, "Z+ hover",  glm::vec3{0.00f, 0.23f, 2.0f}, Mode::Hover);

    m_neg_x_material        = make_material(tools, "X-",        glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Normal);
    m_neg_y_material        = make_material(tools, "Y-",        glm::vec3{0.23f, 1.00f, 0.0f}, Mode::Normal);
    m_neg_z_material        = make_material(tools, "Z-",        glm::vec3{0.00f, 0.23f, 1.0f}, Mode::Normal);
    m_neg_x_hover_material  = make_material(tools, "X- hover",  glm::vec3{3.00f, 0.00f, 0.0f}, Mode::Hover);
    m_neg_y_hover_material  = make_material(tools, "Y- hover",  glm::vec3{0.23f, 3.00f, 0.0f}, Mode::Hover);
    m_neg_z_hover_material  = make_material(tools, "Z- hover",  glm::vec3{0.00f, 0.23f, 3.0f}, Mode::Hover);

    m_pos_x_active_material = make_material(tools, "X+ active", glm::vec3{1.00f, 0.00f, 0.0f}, Mode::Active);
    m_pos_y_active_material = make_material(tools, "Y+ active", glm::vec3{0.00f, 1.00f, 0.0f}, Mode::Active);
    m_pos_z_active_material = make_material(tools, "Z+ active", glm::vec3{0.00f, 0.00f, 1.0f}, Mode::Active);

    m_neg_x_active_material = make_material(tools, "X- active", glm::vec3{0.40f, 0.00f, 0.2f}, Mode::Active);
    m_neg_y_active_material = make_material(tools, "Y- active", glm::vec3{0.20f, 0.40f, 0.0f}, Mode::Active);
    m_neg_z_active_material = make_material(tools, "Z- active", glm::vec3{0.00f, 0.20f, 0.4f}, Mode::Active);

    m_xyz_material          = make_material(tools, "XYZ",        glm::vec3{0.70f, 0.70f, 0.7f}, Mode::Normal);
    m_xyz_hover_material    = make_material(tools, "XYZ hover",  glm::vec3{1.60f, 1.60f, 1.6f}, Mode::Hover);
    m_xyz_active_material   = make_material(tools, "XYZ active", glm::vec3{1.00f, 1.00f, 1.0f}, Mode::Active);

#if 0
    m_pos_x_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    m_pos_y_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    m_pos_z_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    m_neg_x_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    m_neg_y_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
    m_neg_z_material->disable_flag_bits(erhe::Item_flags::show_in_ui);
#else
    m_pos_x_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    m_pos_y_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    m_pos_z_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    m_neg_x_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    m_neg_y_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    m_neg_z_material->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);

#endif

    const auto arrow_cylinder = make_arrow_cylinder(mesh_memory);
    const auto arrow_cone     = make_arrow_cone    (mesh_memory);
    const auto thin_box       = make_box           (mesh_memory, false);
    ////const auto uniform_box    = make_box           (mesh_memory, true);
    const auto center_cube    = make_center_cube   (mesh_memory);
    const auto rotate_ring    = make_rotate_ring   (mesh_memory);
    const auto box_scale_cone = make_box_scale_cone(mesh_memory);

    m_x_arrow_pos_cylinder_mesh  = make_mesh(tools, "+X arrow cylinder", m_pos_x_material, arrow_cylinder);
    m_x_arrow_neg_cylinder_mesh  = make_mesh(tools, "-X arrow cylinder", m_pos_x_material, arrow_cylinder);
    m_x_arrow_pos_cone_mesh      = make_mesh(tools, "+X arrow cone",     m_pos_x_material, arrow_cone    );
    m_x_arrow_neg_cone_mesh      = make_mesh(tools, "-X arrow cone",     m_neg_x_material, arrow_cone    );
    m_y_arrow_pos_cylinder_mesh  = make_mesh(tools, "+Y arrow cylinder", m_pos_y_material, arrow_cylinder);
    m_y_arrow_neg_cylinder_mesh  = make_mesh(tools, "-Y arrow cylinder", m_pos_y_material, arrow_cylinder);
    m_y_arrow_pos_cone_mesh      = make_mesh(tools, "+Y arrow cone",     m_pos_y_material, arrow_cone    );
    m_y_arrow_neg_cone_mesh      = make_mesh(tools, "-Y arrow cone",     m_neg_y_material, arrow_cone    );
    m_z_arrow_pos_cylinder_mesh  = make_mesh(tools, "+Z arrow cylinder", m_pos_z_material, arrow_cylinder);
    m_z_arrow_neg_cylinder_mesh  = make_mesh(tools, "-Z arrow cylinder", m_pos_z_material, arrow_cylinder);
    m_z_arrow_pos_cone_mesh      = make_mesh(tools, "+Z arrow cone",     m_pos_z_material, arrow_cone    );
    m_z_arrow_neg_cone_mesh      = make_mesh(tools, "-Z arrow cone",     m_neg_z_material, arrow_cone    );
    m_xy_translate_box_mesh      = make_mesh(tools, "XY translate box",  m_pos_z_material, thin_box      );
    m_xz_translate_box_mesh      = make_mesh(tools, "XZ translate box",  m_pos_y_material, thin_box      );
    m_yz_translate_box_mesh      = make_mesh(tools, "YZ translate box",  m_pos_x_material, thin_box      );
    m_x_rotate_ring_mesh         = make_mesh(tools, "X rotate ring",     m_pos_x_material, rotate_ring   );
    m_y_rotate_ring_mesh         = make_mesh(tools, "Y rotate ring",     m_pos_y_material, rotate_ring   );
    m_z_rotate_ring_mesh         = make_mesh(tools, "Z rotate ring",     m_pos_z_material, rotate_ring   );
    m_x_pos_scale_mesh           = make_mesh(tools, "+X scale box",      m_pos_x_material, arrow_cone    ); // TODO uniform_box
    m_x_neg_scale_mesh           = make_mesh(tools, "-X scale box",      m_pos_x_material, arrow_cone    ); // TODO uniform_box
    m_y_pos_scale_mesh           = make_mesh(tools, "+Y scale box",      m_pos_y_material, arrow_cone    ); // TODO uniform_box
    m_y_neg_scale_mesh           = make_mesh(tools, "-Y scale box",      m_pos_y_material, arrow_cone    ); // TODO uniform_box
    m_z_pos_scale_mesh           = make_mesh(tools, "+Z scale box",      m_pos_z_material, arrow_cone    ); // TODO uniform_box
    m_z_neg_scale_mesh           = make_mesh(tools, "-Z scale box",      m_pos_z_material, arrow_cone    ); // TODO uniform_box
    m_xy_scale_box_mesh          = make_mesh(tools, "XY scale box",      m_pos_z_material, thin_box      );
    m_xz_scale_box_mesh          = make_mesh(tools, "XZ scale box",      m_pos_y_material, thin_box      );
    m_yz_scale_box_mesh          = make_mesh(tools, "YZ scale box",      m_pos_x_material, thin_box      );
    m_xyz_scale_mesh             = make_mesh(tools, "XYZ scale cube",    m_xyz_material,   center_cube   );
    m_box_scale_pos_x_mesh       = make_mesh(tools, "+X box scale cone", m_pos_x_material, box_scale_cone);
    m_box_scale_neg_x_mesh       = make_mesh(tools, "-X box scale cone", m_neg_x_material, box_scale_cone);
    m_box_scale_pos_y_mesh       = make_mesh(tools, "+Y box scale cone", m_pos_y_material, box_scale_cone);
    m_box_scale_neg_y_mesh       = make_mesh(tools, "-Y box scale cone", m_neg_y_material, box_scale_cone);
    m_box_scale_pos_z_mesh       = make_mesh(tools, "+Z box scale cone", m_pos_z_material, box_scale_cone);
    m_box_scale_neg_z_mesh       = make_mesh(tools, "-Z box scale cone", m_neg_z_material, box_scale_cone);

    // The bounding-box cones live in world space (sized to the selection bounding box),
    // so they hang off m_box_node rather than the view-scaled m_tool_node.
    m_box_scale_pos_x_mesh->get_node()->set_parent(m_box_node);
    m_box_scale_neg_x_mesh->get_node()->set_parent(m_box_node);
    m_box_scale_pos_y_mesh->get_node()->set_parent(m_box_node);
    m_box_scale_neg_y_mesh->get_node()->set_parent(m_box_node);
    m_box_scale_pos_z_mesh->get_node()->set_parent(m_box_node);
    m_box_scale_neg_z_mesh->get_node()->set_parent(m_box_node);

    m_handles[m_box_scale_pos_x_mesh.get()] = Handle::e_handle_box_scale_pos_x;
    m_handles[m_box_scale_neg_x_mesh.get()] = Handle::e_handle_box_scale_neg_x;
    m_handles[m_box_scale_pos_y_mesh.get()] = Handle::e_handle_box_scale_pos_y;
    m_handles[m_box_scale_neg_y_mesh.get()] = Handle::e_handle_box_scale_neg_y;
    m_handles[m_box_scale_pos_z_mesh.get()] = Handle::e_handle_box_scale_pos_z;
    m_handles[m_box_scale_neg_z_mesh.get()] = Handle::e_handle_box_scale_neg_z;

    m_handles[m_x_arrow_pos_cylinder_mesh.get()] = Handle::e_handle_translate_pos_x;
    m_handles[m_x_arrow_neg_cylinder_mesh.get()] = Handle::e_handle_translate_neg_x;
    m_handles[m_x_arrow_neg_cone_mesh    .get()] = Handle::e_handle_translate_pos_x;
    m_handles[m_x_arrow_pos_cone_mesh    .get()] = Handle::e_handle_translate_neg_x;
    m_handles[m_y_arrow_pos_cylinder_mesh.get()] = Handle::e_handle_translate_pos_y;
    m_handles[m_y_arrow_neg_cylinder_mesh.get()] = Handle::e_handle_translate_neg_y;
    m_handles[m_y_arrow_neg_cone_mesh    .get()] = Handle::e_handle_translate_pos_y;
    m_handles[m_y_arrow_pos_cone_mesh    .get()] = Handle::e_handle_translate_neg_y;
    m_handles[m_z_arrow_pos_cylinder_mesh.get()] = Handle::e_handle_translate_pos_z;
    m_handles[m_z_arrow_neg_cylinder_mesh.get()] = Handle::e_handle_translate_neg_z;
    m_handles[m_z_arrow_neg_cone_mesh    .get()] = Handle::e_handle_translate_pos_z;
    m_handles[m_z_arrow_pos_cone_mesh    .get()] = Handle::e_handle_translate_neg_z;
    m_handles[m_xy_translate_box_mesh    .get()] = Handle::e_handle_translate_xy;
    m_handles[m_xz_translate_box_mesh    .get()] = Handle::e_handle_translate_xz;
    m_handles[m_yz_translate_box_mesh    .get()] = Handle::e_handle_translate_yz;
    m_handles[m_x_rotate_ring_mesh       .get()] = Handle::e_handle_rotate_x;
    m_handles[m_y_rotate_ring_mesh       .get()] = Handle::e_handle_rotate_y;
    m_handles[m_z_rotate_ring_mesh       .get()] = Handle::e_handle_rotate_z;
    m_handles[m_x_neg_scale_mesh         .get()] = Handle::e_handle_scale_x;
    m_handles[m_x_pos_scale_mesh         .get()] = Handle::e_handle_scale_x;
    m_handles[m_y_neg_scale_mesh         .get()] = Handle::e_handle_scale_y;
    m_handles[m_y_pos_scale_mesh         .get()] = Handle::e_handle_scale_y;
    m_handles[m_z_neg_scale_mesh         .get()] = Handle::e_handle_scale_z;
    m_handles[m_z_pos_scale_mesh         .get()] = Handle::e_handle_scale_z;
    m_handles[m_xy_scale_box_mesh        .get()] = Handle::e_handle_scale_xy;
    m_handles[m_xz_scale_box_mesh        .get()] = Handle::e_handle_scale_xz;
    m_handles[m_yz_scale_box_mesh        .get()] = Handle::e_handle_scale_yz;
    m_handles[m_xyz_scale_mesh           .get()] = Handle::e_handle_scale_xyz;

    using erhe::scene::Transform;
    using namespace erhe::math;
    const auto translate_cyl_pos_x = Transform{create_translation<float>( 0.5f * arrow_cylinder_length, 0.0f, 0.0f)};
    const auto translate_cyl_neg_x = Transform{create_translation<float>(-0.5f * arrow_cylinder_length, 0.0f, 0.0f)};
    const auto rotate_z_pos_90     = Transform{create_rotation<float>( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f})};
    const auto rotate_z_neg_90     = Transform{create_rotation<float>(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 0.0f, 1.0f})};
    const auto rotate_x_pos_90     = Transform{create_rotation<float>( glm::pi<float>() / 2.0f, glm::vec3{1.0f, 0.0f, 0.0f})};
    const auto rotate_y_pos_90     = Transform{create_rotation<float>( glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f})};
    const auto rotate_y_neg_90     = Transform{create_rotation<float>(-glm::pi<float>() / 2.0f, glm::vec3{0.0f, 1.0f, 0.0f})};
    const auto rotate_y_pos_180    = Transform{create_rotation<float>(-glm::pi<float>()       , glm::vec3{0.0f, 1.0f, 0.0f})};

    m_x_arrow_pos_cylinder_mesh->get_node()->set_parent_from_node(translate_cyl_pos_x);
    m_x_arrow_neg_cylinder_mesh->get_node()->set_parent_from_node(translate_cyl_neg_x);
    m_x_arrow_pos_cone_mesh    ->get_node()->set_parent_from_node(rotate_y_pos_180);
    m_x_arrow_neg_cone_mesh    ->get_node()->set_parent_from_node(glm::mat4{1.0f});

    m_y_arrow_pos_cylinder_mesh->get_node()->set_parent_from_node(rotate_z_pos_90 * translate_cyl_pos_x);
    m_y_arrow_neg_cylinder_mesh->get_node()->set_parent_from_node(rotate_z_pos_90 * translate_cyl_neg_x);
    m_y_arrow_pos_cone_mesh    ->get_node()->set_parent_from_node(rotate_z_neg_90);
    m_y_arrow_neg_cone_mesh    ->get_node()->set_parent_from_node(rotate_z_pos_90);

    m_z_arrow_pos_cylinder_mesh->get_node()->set_parent_from_node(rotate_y_neg_90 * translate_cyl_pos_x);
    m_z_arrow_neg_cylinder_mesh->get_node()->set_parent_from_node(rotate_y_neg_90 * translate_cyl_neg_x);
    m_z_arrow_pos_cone_mesh    ->get_node()->set_parent_from_node(rotate_y_pos_90);
    m_z_arrow_neg_cone_mesh    ->get_node()->set_parent_from_node(rotate_y_neg_90);

    m_xy_translate_box_mesh    ->get_node()->set_parent_from_node(glm::mat4{1.0f});
    m_xz_translate_box_mesh    ->get_node()->set_parent_from_node(rotate_x_pos_90);
    m_yz_translate_box_mesh    ->get_node()->set_parent_from_node(rotate_y_neg_90);

    m_y_rotate_ring_mesh       ->get_node()->set_parent_from_node(glm::mat4{1.0f});
    m_x_rotate_ring_mesh       ->get_node()->set_parent_from_node(rotate_z_pos_90);
    m_z_rotate_ring_mesh       ->get_node()->set_parent_from_node(rotate_x_pos_90);

    m_x_neg_scale_mesh         ->get_node()->set_parent_from_node(rotate_y_pos_180);
    m_x_pos_scale_mesh         ->get_node()->set_parent_from_node(glm::mat4{1.0f});
    m_y_neg_scale_mesh         ->get_node()->set_parent_from_node(rotate_z_neg_90);
    m_y_pos_scale_mesh         ->get_node()->set_parent_from_node(rotate_z_pos_90);
    m_z_neg_scale_mesh         ->get_node()->set_parent_from_node(rotate_y_pos_90);
    m_z_pos_scale_mesh         ->get_node()->set_parent_from_node(rotate_y_neg_90);

    m_xy_scale_box_mesh        ->get_node()->set_parent_from_node(glm::mat4{1.0f});
    m_xz_scale_box_mesh        ->get_node()->set_parent_from_node(rotate_x_pos_90);
    m_yz_scale_box_mesh        ->get_node()->set_parent_from_node(rotate_y_neg_90);

    m_xyz_scale_mesh           ->get_node()->set_parent_from_node(glm::mat4{1.0f});

    //// TODO update_visibility();
}

auto Handle_visualizations::get_handle(erhe::scene::Mesh* mesh) const -> Handle
{
    const auto i = m_handles.find(mesh);
    if (i == m_handles.end()) {
        return Handle::e_handle_none;
    }
    return i->second;
}

void Handle_visualizations::update_for_view(Scene_view* scene_view)
{
    // TODO also consider fov
    m_scene_view = scene_view;
    if (scene_view == nullptr) {
        return;
    }

    const auto camera = scene_view->get_camera();
    if (!camera) {
        return;
    }
    const auto* camera_node = camera->get_node();
    if (camera_node == nullptr) {
        return;
    }

    // TODO Consider fov / ortho size
    // erhe::scene::Projection* projection = camera->projection();

    const glm::vec3 view_position_in_world   = glm::vec3{camera_node->position_in_world()};
    const glm::vec3 anchor_position_in_world = glm::vec3{m_world_from_anchor.get_translation()};
    m_view_distance = glm::length(anchor_position_in_world - glm::vec3{view_position_in_world});
    if (!isfinite(m_view_distance)) {
        log_trs_tool->error("!isfinite()");
    }

}

auto Handle_visualizations::get_gizmo_radius() const -> float
{
    return rotate_ring_major_radius * m_view_scale;
}

auto Handle_visualizations::get_handle_visibility(const Handle handle) const -> bool
{
    auto& shared = m_context.transform_tool->shared;
    switch (get_handle_tool(handle)) {
        case Handle_tool::e_handle_tool_translate: return shared.settings.show_translate;
        case Handle_tool::e_handle_tool_rotate:    return shared.settings.show_rotate;
        case Handle_tool::e_handle_tool_scale:     return shared.settings.show_scale;
        default:                                   return false;
    }
}

void Handle_visualizations::update_mesh_visibility(bool precondition, const std::shared_ptr<erhe::scene::Mesh>& mesh)
{
    auto& transform_tool = *m_context.transform_tool;
    const auto active_handle = transform_tool.get_active_handle();
    const auto hover_handle  = transform_tool.get_hover_handle();
    const bool show_all      = (active_handle == Handle::e_handle_none); // nothing is active, so show all handles
    const auto handle        = get_handle(mesh.get());
    const auto axis_mask     = get_axis_mask(handle);
    const bool show          = get_handle_visibility(handle);
    const bool translate     = m_context.move_tool ->is_active() && ((m_context.move_tool ->get_axis_mask() & axis_mask) == axis_mask);
    const bool scale         = m_context.scale_tool->is_active() && ((m_context.scale_tool->get_axis_mask() & axis_mask) == axis_mask);

    const bool visible =
        precondition &&
        (!transform_tool.shared.entries.empty() || transform_tool.shared.component_mode) &&
        show &&
        (
            !transform_tool.shared.settings.hide_inactive ||
            (active_handle == handle) ||
            translate ||
            scale ||
            show_all
        );

    erhe::scene::Node* node = mesh->get_node();
    const bool is_visible = node->is_visible();
    if (is_visible != visible) {
        node->set_visible(visible);
    }

    const Mode mode = (active_handle == handle) || translate
        ? Mode::Active
        : (hover_handle == handle)
            ? Mode::Hover
            : Mode::Normal;

    const auto& material = get_handle_material(handle, mode);

    // get_handle_material() returns null for handles with no mapped material (its default
    // case). Never store a null material into the primitive - keep the existing one - so the
    // renderer is not handed a null material to dereference.
    if (material) {
        mesh->get_mutable_primitives().front().material = material;
    }

    // log_trs_tool->trace(
    //     "{}->set_visible({}) mode = {}, material = {}",
    //     node->get_name(), visible, c_str(mode), material ? material->get_name() : std::string{"<no material>"}
    // );
}

void Handle_visualizations::update_visibility(Transform_tool_settings& settings)
{
    const bool translate  = settings.show_translate;
    const bool rotate     = settings.show_rotate;
    const bool scale      = settings.show_scale;
    const bool scale_box  = scale && (settings.scale_gizmo_mode == Scale_gizmo_mode::bounding_box);
    const bool scale_axis = scale && (settings.scale_gizmo_mode == Scale_gizmo_mode::basic);
    update_mesh_visibility(translate, m_x_arrow_pos_cylinder_mesh);
    update_mesh_visibility(translate, m_x_arrow_neg_cylinder_mesh);
    update_mesh_visibility(translate, m_x_arrow_pos_cone_mesh);
    update_mesh_visibility(translate, m_x_arrow_neg_cone_mesh);
    update_mesh_visibility(translate, m_y_arrow_pos_cylinder_mesh);
    update_mesh_visibility(translate, m_y_arrow_neg_cylinder_mesh);
    update_mesh_visibility(translate, m_y_arrow_pos_cone_mesh);
    update_mesh_visibility(translate, m_y_arrow_neg_cone_mesh);
    update_mesh_visibility(translate, m_z_arrow_pos_cylinder_mesh);
    update_mesh_visibility(translate, m_z_arrow_neg_cylinder_mesh);
    update_mesh_visibility(translate, m_z_arrow_pos_cone_mesh);
    update_mesh_visibility(translate, m_z_arrow_neg_cone_mesh);
    update_mesh_visibility(translate, m_xy_translate_box_mesh);
    update_mesh_visibility(translate, m_xz_translate_box_mesh);
    update_mesh_visibility(translate, m_yz_translate_box_mesh);
    update_mesh_visibility(rotate,    m_x_rotate_ring_mesh   );
    update_mesh_visibility(rotate,    m_y_rotate_ring_mesh   );
    update_mesh_visibility(rotate,    m_z_rotate_ring_mesh   );
    update_mesh_visibility(scale_axis, m_x_neg_scale_mesh     );
    update_mesh_visibility(scale_axis, m_x_pos_scale_mesh     );
    update_mesh_visibility(scale_axis, m_y_neg_scale_mesh     );
    update_mesh_visibility(scale_axis, m_y_pos_scale_mesh     );
    update_mesh_visibility(scale_axis, m_z_neg_scale_mesh     );
    update_mesh_visibility(scale_axis, m_z_pos_scale_mesh     );
    update_mesh_visibility(scale_axis, m_xy_scale_box_mesh    );
    update_mesh_visibility(scale_axis, m_xz_scale_box_mesh    );
    update_mesh_visibility(scale_axis, m_yz_scale_box_mesh    );
    // The uniform-scale cube is shown in both scale gizmo modes: the bounding-box mode's
    // face cones only offer per-axis scaling, so it needs the uniform handle too.
    update_mesh_visibility(scale,      m_xyz_scale_mesh       );
    update_mesh_visibility(scale_box,  m_box_scale_pos_x_mesh );
    update_mesh_visibility(scale_box,  m_box_scale_neg_x_mesh );
    update_mesh_visibility(scale_box,  m_box_scale_pos_y_mesh );
    update_mesh_visibility(scale_box,  m_box_scale_neg_y_mesh );
    update_mesh_visibility(scale_box,  m_box_scale_pos_z_mesh );
    update_mesh_visibility(scale_box,  m_box_scale_neg_z_mesh );
}

auto Handle_visualizations::get_mode_material(
    const Mode                                        mode,
    const std::shared_ptr<erhe::primitive::Material>& active,
    const std::shared_ptr<erhe::primitive::Material>& hover,
    const std::shared_ptr<erhe::primitive::Material>& normal
) -> std::shared_ptr<erhe::primitive::Material>
{
    switch (mode) {
        case Mode::Normal: return normal;
        case Mode::Active: return active;
        case Mode::Hover:  return hover;
        default:           return {};
    }
}

auto Handle_visualizations::get_handle_material(const Handle handle, const Mode mode) -> std::shared_ptr<erhe::primitive::Material>
{
    switch (handle) {
        case Handle::e_handle_translate_pos_x: return get_mode_material(mode, m_pos_x_active_material, m_pos_x_hover_material, m_pos_x_material);
        case Handle::e_handle_translate_neg_x: return get_mode_material(mode, m_neg_x_active_material, m_neg_x_hover_material, m_neg_x_material);
        case Handle::e_handle_translate_pos_y: return get_mode_material(mode, m_pos_y_active_material, m_pos_y_hover_material, m_pos_y_material);
        case Handle::e_handle_translate_neg_y: return get_mode_material(mode, m_neg_y_active_material, m_neg_y_hover_material, m_neg_y_material);
        case Handle::e_handle_translate_pos_z: return get_mode_material(mode, m_pos_z_active_material, m_pos_z_hover_material, m_pos_z_material);
        case Handle::e_handle_translate_neg_z: return get_mode_material(mode, m_neg_z_active_material, m_neg_z_hover_material, m_neg_z_material);
        case Handle::e_handle_translate_xy   : return get_mode_material(mode, m_pos_z_active_material, m_pos_z_hover_material, m_pos_z_material);
        case Handle::e_handle_translate_xz   : return get_mode_material(mode, m_pos_y_active_material, m_pos_y_hover_material, m_pos_y_material);
        case Handle::e_handle_translate_yz   : return get_mode_material(mode, m_pos_x_active_material, m_pos_x_hover_material, m_pos_x_material);
        case Handle::e_handle_rotate_x       : return get_mode_material(mode, m_pos_x_active_material, m_pos_x_hover_material, m_pos_x_material);
        case Handle::e_handle_rotate_y       : return get_mode_material(mode, m_pos_y_active_material, m_pos_y_hover_material, m_pos_y_material);
        case Handle::e_handle_rotate_z       : return get_mode_material(mode, m_pos_z_active_material, m_pos_z_hover_material, m_pos_z_material);
        case Handle::e_handle_scale_x        : return get_mode_material(mode, m_pos_x_active_material, m_pos_x_hover_material, m_pos_x_material);
        case Handle::e_handle_scale_y        : return get_mode_material(mode, m_pos_y_active_material, m_pos_y_hover_material, m_pos_y_material);
        case Handle::e_handle_scale_z        : return get_mode_material(mode, m_pos_z_active_material, m_pos_z_hover_material, m_pos_z_material);
        case Handle::e_handle_scale_xyz      : return get_mode_material(mode, m_xyz_active_material,   m_xyz_hover_material,   m_xyz_material);
        case Handle::e_handle_box_scale_pos_x: return get_mode_material(mode, m_pos_x_active_material, m_pos_x_hover_material, m_pos_x_material);
        case Handle::e_handle_box_scale_neg_x: return get_mode_material(mode, m_neg_x_active_material, m_neg_x_hover_material, m_neg_x_material);
        case Handle::e_handle_box_scale_pos_y: return get_mode_material(mode, m_pos_y_active_material, m_pos_y_hover_material, m_pos_y_material);
        case Handle::e_handle_box_scale_neg_y: return get_mode_material(mode, m_neg_y_active_material, m_neg_y_hover_material, m_neg_y_material);
        case Handle::e_handle_box_scale_pos_z: return get_mode_material(mode, m_pos_z_active_material, m_pos_z_hover_material, m_pos_z_material);
        case Handle::e_handle_box_scale_neg_z: return get_mode_material(mode, m_neg_z_active_material, m_neg_z_hover_material, m_neg_z_material);
        // TODO
        default: return {};
    }
}

#pragma region Make handles
auto Handle_visualizations::make_mesh(
    Tools&                                            tools,
    const std::string_view                            name,
    const std::shared_ptr<erhe::primitive::Material>& material,
    const Part&                                       part
) -> std::shared_ptr<erhe::scene::Mesh>
{
    ERHE_PROFILE_FUNCTION();
    auto node = std::make_shared<erhe::scene::Node>(name);
    auto mesh = std::make_shared<erhe::scene::Mesh>(name);
    mesh->add_primitive(part.primitive, material);
    ERHE_VERIFY(mesh->get_mutable_primitives().front().primitive->has_raytrace_triangles());

    mesh->enable_flag_bits(erhe::Item_flags::tool | erhe::Item_flags::id);
    node->attach(mesh);

    const auto scene_root    = tools.get_tool_scene_root();
    const auto tool_layer_id = scene_root->layers().tool()->id;
    mesh->layer_id = tool_layer_id;
    node->set_parent(m_tool_node);
    return mesh;
}

Handle_visualizations::Part::Part(
    erhe::scene_renderer::Mesh_memory&               mesh_memory,
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry
)
    : primitive{
        std::make_shared<erhe::primitive::Primitive>(render_geometry, collision_geometry)
    }
{
    const bool render_ok = primitive->make_renderable_mesh(
        erhe::primitive::Build_info{
            .primitive_types{
                .fill_triangles = true
            },
            .buffer_info = mesh_memory.make_primitive_buffer_info()
        },
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(render_ok);

    const bool raytrace_ok = primitive->make_raytrace();
    ERHE_VERIFY(raytrace_ok);
}

auto Handle_visualizations::make_arrow_cylinder(erhe::scene_renderer::Mesh_memory& mesh_memory) -> Part
{
    ERHE_PROFILE_FUNCTION();

    auto render_geometry = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_cylinder(
        render_geometry->get_mesh(),
        -arrow_cylinder_length / 2.0f,
        arrow_cylinder_length / 2.0f,
        arrow_cylinder_radius_render,
        true,
        true,
        32,
        4
    );
    erhe::geometry::shapes::make_cylinder(
        raytrace_geometry->get_mesh(),
        -arrow_cylinder_length / 2.0f,
        arrow_cylinder_length / 2.0f,
        arrow_cylinder_radius_collision,
        true,
        true,
        20,
        1
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_arrow_cone(erhe::scene_renderer::Mesh_memory& mesh_memory) -> Part
{
    ERHE_PROFILE_FUNCTION();

    auto render_geometry = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_cone(
        render_geometry->get_mesh(),
        arrow_cylinder_length,
        arrow_tip_render,
        arrow_cone_radius_render,
        true,
        32,
        4
    );
    erhe::geometry::shapes::make_cone(
        raytrace_geometry->get_mesh(),
        arrow_cylinder_length,
        arrow_tip_collision,
        arrow_cone_radius_collision,
        true,
        22,
        1
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_box(erhe::scene_renderer::Mesh_memory& mesh_memory, const bool uniform) -> Part
{
    ERHE_PROFILE_FUNCTION();

    auto render_geometry = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_box(
        render_geometry->get_mesh(),
        -box_length_render,
         box_length_render,
        -box_length_render,
         box_length_render,
        (uniform ? -box_length_render : -box_half_thickness_render),
        (uniform ?  box_length_render :  box_half_thickness_render)
    );
    erhe::geometry::shapes::make_box(
        raytrace_geometry->get_mesh(),
        -box_length_collision,
         box_length_collision,
        -box_length_collision,
         box_length_collision,
        (uniform ? -box_length_collision : -box_half_thickness_collision),
        (uniform ?  box_length_collision :  box_half_thickness_collision)
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_center_cube(erhe::scene_renderer::Mesh_memory& mesh_memory) -> Part
{
    ERHE_PROFILE_FUNCTION();

    auto render_geometry   = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_box(
        render_geometry->get_mesh(),
        -center_cube_half_length_render,
         center_cube_half_length_render,
        -center_cube_half_length_render,
         center_cube_half_length_render,
        -center_cube_half_length_render,
         center_cube_half_length_render
    );
    erhe::geometry::shapes::make_box(
        raytrace_geometry->get_mesh(),
        -center_cube_half_length_collision,
         center_cube_half_length_collision,
        -center_cube_half_length_collision,
         center_cube_half_length_collision,
        -center_cube_half_length_collision,
         center_cube_half_length_collision
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_box_scale_cone(erhe::scene_renderer::Mesh_memory& mesh_memory) -> Part
{
    ERHE_PROFILE_FUNCTION();

    auto render_geometry   = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_cone(
        render_geometry->get_mesh(),
        -box_scale_cone_half_length_render,
         box_scale_cone_half_length_render,
        box_scale_cone_radius_render,
        true,
        32,
        4
    );
    erhe::geometry::shapes::make_cone(
        raytrace_geometry->get_mesh(),
        -box_scale_cone_half_length_collision,
         box_scale_cone_half_length_collision,
        box_scale_cone_radius_collision,
        true,
        22,
        1
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_rotate_ring(erhe::scene_renderer::Mesh_memory& mesh_memory) -> Part
{
    ERHE_PROFILE_FUNCTION();
    auto render_geometry = std::make_shared<erhe::geometry::Geometry>();
    auto raytrace_geometry = std::make_shared<erhe::geometry::Geometry>();
    erhe::geometry::shapes::make_torus(
        render_geometry->get_mesh(),
        rotate_ring_major_radius,
        rotate_ring_minor_radius_render,
        80,
        32
    );
    erhe::geometry::shapes::make_torus(
        raytrace_geometry->get_mesh(),
        rotate_ring_major_radius,
        rotate_ring_minor_radius_collision,
        80,
        20
    );
    return Part{mesh_memory, render_geometry, raytrace_geometry};
}

auto Handle_visualizations::make_material(
    Tools&           tools,
    const char*      name,
    const glm::vec3& color,
    const Mode       mode
) -> std::shared_ptr<erhe::primitive::Material>
{
    auto& material_library = tools.get_tool_scene_root()->get_content_library()->materials;
    switch (mode) {
        case Mode::Normal: {
            return material_library->make<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{
                    .name = name,
                    .data = {
                        .base_color = color,
                        .bxdf_model = erhe::primitive::Bxdf_model::unlit
                    }
                }
            );
        }
        case Mode::Active: {
            // TODO ? glm::vec3(1.0f, 0.7f, 0.1f));
            return material_library->make<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{
                    .name = name,
                    .data = {
                        .base_color = color,
                        .bxdf_model = erhe::primitive::Bxdf_model::unlit
                    }
                }
            );
        }
        case Mode::Hover: {
            // TODO ? 2.0f * color);
            return material_library->make<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{
                    .name = name,
                    .data = {
                        .base_color = color,
                        .bxdf_model = erhe::primitive::Bxdf_model::unlit
                    }
                }
            );
        }
        default: {
            return {};
        }
    }
}
#pragma endregion Make handles

void Handle_visualizations::update_transforms() //const uint64_t serial)
{
    ERHE_PROFILE_FUNCTION();

    if (m_scene_view == nullptr) {
        return;
    }

    const auto& settings          = m_context.transform_tool->shared.settings;
    const float distance_scale    = m_context.editor_settings->gizmo_scale * m_view_distance / 100.0f;
    const float perspective_scale = m_scene_view->get_perspective_scale();
    const float scalar_scale      = distance_scale * perspective_scale;

    if (!isfinite(scalar_scale)) {
        log_trs_tool->error("!isfinite()");
    }
    m_view_scale = scalar_scale;
    const glm::mat4 scale             = erhe::math::create_scale<float>(scalar_scale);
    // The gizmo follows only the anchor's position and orientation, never its
    // scale or skew (a scaled/skewed selection must not distort the handles).
    const glm::mat4 world_from_anchor = settings.use_anchor_orientation()
        ? erhe::math::create_translation<float>(m_world_from_anchor.get_translation()) * glm::mat4_cast(m_world_from_anchor.get_rotation())
        : erhe::math::create_translation<float>(m_world_from_anchor.get_translation());
    const glm::vec3 origin = glm::vec3{world_from_anchor * glm::vec4{0.0f, 0.0, 0.0f, 1.0}};

    m_tool_node->set_parent_from_node(world_from_anchor * scale);

    compute_selection_box();
    update_box_handles();
}

void Handle_visualizations::compute_selection_box()
{
    m_box_valid = false;

    // Use a rigid box frame (translation + rotation, no scale). This keeps the frame
    // invertible even when the selection has a zero scale component, and makes the box
    // AABB measure true world-space extents (so the drag math and the degenerate-recovery
    // path both work in world units).
    const Transform_tool_shared&   shared    = m_context.transform_tool->shared;
    const Transform_tool_settings& settings  = shared.settings;
    const glm::mat4                box_frame = settings.use_anchor_orientation()
        ? erhe::math::create_translation<float>(shared.world_from_anchor.get_translation()) * glm::mat4_cast(shared.world_from_anchor.get_rotation())
        : erhe::math::create_translation<float>(shared.world_from_anchor.get_translation());
    const glm::mat4 box_inv = glm::inverse(box_frame);
    m_box_frame = box_frame;

    erhe::math::Aabb aabb{};
    bool             any{false};
    for (const Transform_entry& entry : shared.entries) {
        const std::shared_ptr<erhe::scene::Node>& node = entry.node;
        if (!node) {
            continue;
        }
        const glm::mat4 box_from_node = box_inv * node->world_from_node();
        std::shared_ptr<erhe::scene::Mesh> mesh = erhe::scene::get_attachment<erhe::scene::Mesh>(node.get());
        if (mesh) {
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                if (!mesh_primitive.primitive) {
                    continue;
                }
                const erhe::math::Aabb local = mesh_primitive.primitive->get_bounding_box();
                if (!local.is_valid()) {
                    continue;
                }
                aabb.include(local.transformed_by(box_from_node));
                any = true;
            }
        } else {
            aabb.include(glm::vec3{box_from_node * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}});
            any = true;
        }
    }

    m_box_aabb  = aabb;
    m_box_valid = any && aabb.is_valid();
}

void Handle_visualizations::update_box_handles()
{
    if (!m_box_valid || (m_scene_view == nullptr)) {
        return;
    }

    const Transform_tool_shared&   shared   = m_context.transform_tool->shared;
    const Transform_tool_settings& settings = shared.settings;

    const float     distance_scale    = m_context.editor_settings->gizmo_scale * m_view_distance / 100.0f;
    const float     perspective_scale = m_scene_view->get_perspective_scale();
    const float     scalar_scale      = distance_scale * perspective_scale;
    const glm::mat4 cone_scale         = erhe::math::create_scale<float>(scalar_scale);
    const glm::mat4 orient             = settings.use_anchor_orientation()
        ? glm::mat4_cast(shared.world_from_anchor.get_rotation())
        : glm::mat4{1.0f};

    using erhe::math::create_rotation;
    const glm::mat4 r_pos_x{1.0f};
    const glm::mat4 r_neg_x = create_rotation<float>(-glm::pi<float>(),         glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 r_pos_y = create_rotation<float>( glm::pi<float>() / 2.0f,  glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::mat4 r_neg_y = create_rotation<float>(-glm::pi<float>() / 2.0f,  glm::vec3{0.0f, 0.0f, 1.0f});
    const glm::mat4 r_pos_z = create_rotation<float>(-glm::pi<float>() / 2.0f,  glm::vec3{0.0f, 1.0f, 0.0f});
    const glm::mat4 r_neg_z = create_rotation<float>( glm::pi<float>() / 2.0f,  glm::vec3{0.0f, 1.0f, 0.0f});

    const glm::vec3 center = m_box_aabb.center();
    const glm::vec3 mn     = m_box_aabb.min;
    const glm::vec3 mx     = m_box_aabb.max;

    // The cone geometry is centered on its local origin (base at -half, apex at +half along
    // +X). Offset it outward by half its length so the base rests on the bounding-box face
    // and the tip points away from the box.
    const glm::mat4 cone_base_on_face = erhe::math::create_translation<float>(
        glm::vec3{box_scale_cone_half_length_render, 0.0f, 0.0f}
    );
    const auto place = [&](const std::shared_ptr<erhe::scene::Mesh>& mesh, const glm::vec3 face_center_box, const glm::mat4& r_axis) {
        const glm::vec3 world_pos = glm::vec3{m_box_frame * glm::vec4{face_center_box, 1.0f}};
        const glm::mat4 m         = erhe::math::create_translation<float>(world_pos) * orient * r_axis * cone_scale * cone_base_on_face;
        mesh->get_node()->set_parent_from_node(m);
    };

    place(m_box_scale_pos_x_mesh, glm::vec3{mx.x,     center.y, center.z}, r_pos_x);
    place(m_box_scale_neg_x_mesh, glm::vec3{mn.x,     center.y, center.z}, r_neg_x);
    place(m_box_scale_pos_y_mesh, glm::vec3{center.x, mx.y,     center.z}, r_pos_y);
    place(m_box_scale_neg_y_mesh, glm::vec3{center.x, mn.y,     center.z}, r_neg_y);
    place(m_box_scale_pos_z_mesh, glm::vec3{center.x, center.y, mx.z    }, r_pos_z);
    place(m_box_scale_neg_z_mesh, glm::vec3{center.x, center.y, mn.z    }, r_neg_z);
}

void Handle_visualizations::set_anchor(const erhe::scene::Trs_transform& world_from_anchor)
{
    m_world_from_anchor = world_from_anchor;
}

void Handle_visualizations::viewport_toolbar()
{
    ImGui::PushID("Handle_visualizations::viewport_toolbar");
    const auto& icon_set = m_context.icon_set;

    auto& settings = m_context.transform_tool->shared.settings;
    const auto reference_mode_button = [&](const char* label, const Transform_reference_mode mode, const char* tooltip) {
        const bool pressed = erhe::imgui::make_button(label, (settings.reference_mode == mode) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
        if (pressed && (settings.reference_mode != mode)) {
            settings.reference_mode = mode;
            m_context.transform_tool->on_reference_settings_changed();
        }
        ImGui::SameLine();
    };
    reference_mode_button("W", Transform_reference_mode::global,    "Transform in World space");
    reference_mode_button("L", Transform_reference_mode::local,     "Transform in Local space");
    reference_mode_button("R", Transform_reference_mode::reference, "Transform in Reference node space");
    reference_mode_button("S", Transform_reference_mode::selection, "Transform in mesh component Selection space");

    {
        const auto mode = settings.show_translate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;

        erhe::imgui::begin_button_style(mode);
        const bool translate_pressed = icon_set->icon_button(ERHE_HASH("move"), m_context.icon_set->icons.move);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_translate ? "Hide Translate Tool" : "Show Translate Tool");
        }
        if (translate_pressed) {
            settings.show_translate = !settings.show_translate;
            update_visibility(settings);
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_rotate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool rotate_pressed = icon_set->icon_button(ERHE_HASH("rotate"), m_context.icon_set->icons.rotate);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_rotate ? "Hide Rotate Tool" : "Show Rotate Tool");
        }
        if (rotate_pressed) {
            settings.show_rotate = !settings.show_rotate;
            update_visibility(settings);
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_scale ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool scale_pressed = icon_set->icon_button(ERHE_HASH("scale"), m_context.icon_set->icons.scale);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(settings.show_scale ? "Hide Scale Tool" : "Show Scale Tool");
        }
        if (scale_pressed) {
            settings.show_scale = !settings.show_scale;
            update_visibility(settings);
        }
    }

    ImGui::PopID();
}

}
