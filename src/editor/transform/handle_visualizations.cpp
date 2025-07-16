#include "transform/handle_visualizations.hpp"

#include "app_context.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "graphics/icon_set.hpp"
#include "renderers/mesh_memory.hpp"
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
#include "erhe_scene/scene.hpp"
#include "erhe_hash/xxhash.hpp"
#include "erhe_profile/profile.hpp"

namespace editor {

namespace {
    constexpr float arrow_cylinder_length              = 2.75f;
    constexpr float arrow_cylinder_radius_render       = 0.03f;
    constexpr float arrow_cylinder_radius_collision    = arrow_cylinder_radius_render * 3.0f;
    constexpr float arrow_cone_length_render           = 0.6f;
    constexpr float arrow_cone_length_collision        = arrow_cone_length_render * 2.0f;
    constexpr float arrow_cone_radius_render           = 0.15f;
    constexpr float arrow_cone_radius_collision        = arrow_cone_radius_render * 3.0f;
    constexpr float box_half_thickness_render          = 0.1f;
    constexpr float box_half_thickness_collision       = box_half_thickness_render * 2.0f;
    constexpr float box_length_render                  = 0.6f;
    constexpr float box_length_collision               = box_length_render * 1.3f;
    constexpr float rotate_ring_major_radius           = 4.0f;
    constexpr float rotate_ring_minor_radius_render    = 0.05f;
    constexpr float rotate_ring_minor_radius_collision = rotate_ring_minor_radius_render * 4.0f;

    constexpr float arrow_tip_render = arrow_cylinder_length + arrow_cone_length_render;
    constexpr float arrow_tip_collision = arrow_cylinder_length + arrow_cone_length_collision;
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

Handle_visualizations::Handle_visualizations(App_context& app_context, Mesh_memory& mesh_memory, Tools& tools)
    : m_context{app_context}
{
    ERHE_PROFILE_FUNCTION();

    m_tool_node = std::make_shared<erhe::scene::Node>("Trs");
    const auto scene_root = tools.get_tool_scene_root();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> scene_lock{scene_root->item_host_mutex};

    m_tool_node->set_parent(scene_root->get_hosted_scene()->get_root_node());

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

    erhe::graphics::Buffer_transfer_queue buffer_transfer_queue{mesh_memory.graphics_device};

    const auto arrow_cylinder = make_arrow_cylinder(mesh_memory);
    const auto arrow_cone     = make_arrow_cone    (mesh_memory);
    const auto thin_box       = make_box           (mesh_memory, false);
    ////const auto uniform_box    = make_box           (mesh_memory, true);
    const auto rotate_ring    = make_rotate_ring   (mesh_memory);

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

void Handle_visualizations::update_mesh_visibility(const std::shared_ptr<erhe::scene::Mesh>& mesh)
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
        !transform_tool.shared.entries.empty() && 
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

    mesh->get_mutable_primitives().front().material = material;

    log_trs_tool->trace(
        "{}->set_visible({}) mode = {}, material = {}",
        node->get_name(), visible, c_str(mode), material->get_name()
    );
}

void Handle_visualizations::update_visibility()
{
    update_mesh_visibility(m_x_arrow_pos_cylinder_mesh);
    update_mesh_visibility(m_x_arrow_neg_cylinder_mesh);
    update_mesh_visibility(m_x_arrow_pos_cone_mesh);
    update_mesh_visibility(m_x_arrow_neg_cone_mesh);
    update_mesh_visibility(m_y_arrow_pos_cylinder_mesh);
    update_mesh_visibility(m_y_arrow_neg_cylinder_mesh);
    update_mesh_visibility(m_y_arrow_pos_cone_mesh);
    update_mesh_visibility(m_y_arrow_neg_cone_mesh);
    update_mesh_visibility(m_z_arrow_pos_cylinder_mesh);
    update_mesh_visibility(m_z_arrow_neg_cylinder_mesh);
    update_mesh_visibility(m_z_arrow_pos_cone_mesh);
    update_mesh_visibility(m_z_arrow_neg_cone_mesh);
    update_mesh_visibility(m_xy_translate_box_mesh);
    update_mesh_visibility(m_xz_translate_box_mesh);
    update_mesh_visibility(m_yz_translate_box_mesh);
    update_mesh_visibility(m_x_rotate_ring_mesh   );
    update_mesh_visibility(m_y_rotate_ring_mesh   );
    update_mesh_visibility(m_z_rotate_ring_mesh   );
    update_mesh_visibility(m_x_neg_scale_mesh     );
    update_mesh_visibility(m_x_pos_scale_mesh     );
    update_mesh_visibility(m_y_neg_scale_mesh     );
    update_mesh_visibility(m_y_pos_scale_mesh     );
    update_mesh_visibility(m_z_neg_scale_mesh     );
    update_mesh_visibility(m_z_pos_scale_mesh     );
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
    ERHE_VERIFY(mesh->get_mutable_primitives().front().has_raytrace_triangles());

    mesh->enable_flag_bits(erhe::Item_flags::tool | erhe::Item_flags::id);
    node->attach(mesh);

    const auto scene_root    = tools.get_tool_scene_root();
    const auto tool_layer_id = scene_root->layers().tool()->id;
    mesh->layer_id = tool_layer_id;
    node->set_parent(m_tool_node);
    return mesh;
}

Handle_visualizations::Part::Part(
    Mesh_memory&                                     mesh_memory,
    const std::shared_ptr<erhe::geometry::Geometry>& render_geometry,
    const std::shared_ptr<erhe::geometry::Geometry>& collision_geometry
)
    : primitive{render_geometry, collision_geometry}
{
    const bool render_ok = primitive.make_renderable_mesh(
        erhe::primitive::Build_info{
            .primitive_types{
                .fill_triangles = true
            },
            .buffer_info = mesh_memory.buffer_info
        },
        erhe::primitive::Normal_style::corner_normals
    );
    ERHE_VERIFY(render_ok);

    const bool raytrace_ok = primitive.make_raytrace();
    ERHE_VERIFY(raytrace_ok);
}

auto Handle_visualizations::make_arrow_cylinder(Mesh_memory& mesh_memory) -> Part
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

auto Handle_visualizations::make_arrow_cone(Mesh_memory& mesh_memory) -> Part
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

auto Handle_visualizations::make_box(Mesh_memory& mesh_memory, const bool uniform) -> Part
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

auto Handle_visualizations::make_rotate_ring(Mesh_memory& mesh_memory) -> Part
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
                    .name       = name,
                    .base_color = color
                }
            );
        }
        case Mode::Active: {
            // TODO ? glm::vec3(1.0f, 0.7f, 0.1f));
            return material_library->make<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{
                    .name       = name,
                    .base_color = color
                }
            );
        }
        case Mode::Hover: {
            // TODO ? 2.0f * color);
            return material_library->make<erhe::primitive::Material>(
                erhe::primitive::Material_create_info{
                    .name       = name,
                    .base_color = color
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

    const auto& settings     = m_context.transform_tool->shared.settings;
    const float scalar_scale = m_scene_view->get_config().gizmo_scale * m_view_distance / 100.0f;

    if (!isfinite(scalar_scale)) {
        log_trs_tool->error("!isfinite()");
    }
    const glm::mat4 scale             = erhe::math::create_scale<float>(scalar_scale);
    const glm::mat4 world_from_anchor = settings.local
        ? m_world_from_anchor.get_matrix()
        : erhe::math::create_translation<float>(m_world_from_anchor.get_translation());
    const glm::vec3 origin = glm::vec3{world_from_anchor * glm::vec4{0.0f, 0.0, 0.0f, 1.0}};

    m_tool_node->set_parent_from_node(world_from_anchor * scale);
}

void Handle_visualizations::set_anchor(const erhe::scene::Trs_transform& world_from_anchor)
{
    m_world_from_anchor = world_from_anchor;
}

void Handle_visualizations::viewport_toolbar(bool& hovered)
{
    ImGui::PushID("Handle_visualizations::viewport_toolbar");
    const auto& icon_set = m_context.icon_set;

    auto& settings = m_context.transform_tool->shared.settings;
    const auto local_pressed = erhe::imgui::make_button("L", settings.local ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip("Transform in Local space");
    }
    if (local_pressed) {
        settings.local = true;
    }

    ImGui::SameLine();
    const auto global_pressed = erhe::imgui::make_button("W", (!settings.local) ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal);
    if (ImGui::IsItemHovered()) {
        hovered = true;
        ImGui::SetTooltip("Transform in World space");
    }
    if (global_pressed) {
        settings.local = false;
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_translate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;

        erhe::imgui::begin_button_style(mode);
        const bool translate_pressed = icon_set->icon_button(ERHE_HASH("move"), m_context.icon_set->icons.move);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            hovered = true;
            ImGui::SetTooltip(settings.show_translate ? "Hide Translate Tool" : "Show Translate Tool");
        }
        if (translate_pressed) {
            settings.show_translate = !settings.show_translate;
            update_visibility();
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_rotate ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool rotate_pressed = icon_set->icon_button(ERHE_HASH("rotate"), m_context.icon_set->icons.rotate);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            hovered = true;
            ImGui::SetTooltip(settings.show_rotate ? "Hide Rotate Tool" : "Show Rotate Tool");
        }
        if (rotate_pressed) {
            settings.show_rotate = !settings.show_rotate;
            update_visibility();
        }
    }

    ImGui::SameLine();
    {
        const auto mode = settings.show_scale ? erhe::imgui::Item_mode::active : erhe::imgui::Item_mode::normal;
        erhe::imgui::begin_button_style(mode);
        const bool scale_pressed = icon_set->icon_button(ERHE_HASH("scale"), m_context.icon_set->icons.scale);
        erhe::imgui::end_button_style(mode);
        if (ImGui::IsItemHovered()) {
            hovered = true;
            ImGui::SetTooltip(settings.show_scale ? "Hide Scale Tool" : "Show Scale Tool");
        }
        if (scale_pressed) {
            settings.show_scale = !settings.show_scale;
            update_visibility();
        }
    }

    ImGui::PopID();
}

}
