#include "windows/properties.hpp"

#include "editor_context.hpp"
#include "editor_message_bus.hpp"
#include "rendertarget_mesh.hpp"
#include "tools/selection_tool.hpp"
#include "scene/content_library.hpp"
#include "scene/frame_controller.hpp"
#include "scene/material_preview.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "windows/animation_curve.hpp"
#include "windows/brdf_slice.hpp"

#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_imgui/imgui_helpers.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_primitive/geometry_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/animation.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_scene/skin.hpp"
#include "erhe_bit/bit_helpers.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui/imgui.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

const float indent = 15.0f;

Properties::Properties(
    erhe::imgui::Imgui_renderer& imgui_renderer,
    erhe::imgui::Imgui_windows&  imgui_windows,
    Editor_context&              editor_context
)
    : Imgui_window{imgui_renderer, imgui_windows, "Properties", "properties"}
    , m_context   {editor_context}
{
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
void Properties::animation_properties(erhe::scene::Animation& animation) const
{
    ImGui::Text("Samplers: %d", static_cast<int>(animation.samplers.size()));
    ImGui::Text("Channels: %d", static_cast<int>(animation.channels.size()));
    //ImGui::BulletText("Samplers");
    //{
    //    for (auto& sampler : animation.samplers) {
    //        std::string text = fmt::format("Sampler {}", erhe::scene::c_str(sampler.interpolation_mode));
    //        ImGui::BulletText("%s", text.c_str());
    //    }
    //}
    //
    //ImGui::BulletText("Channels");
    //ImGui::Indent(10.0f);
    //{
    //    for (auto& channel : animation->channels) {
    //        std::string text = fmt::format(
    //            "{} {}",
    //            //channel.target->get_name(),
    //            channel.target->describe(),
    //            erhe::scene::c_str(channel.path)
    //        );
    //        ImGui::BulletText("%s", text.c_str());
    //    }
    //}
    //ImGui::Unindent(10.0f);

    animation_curve(animation);

    static float time       = 0.0f;
    static float start_time = 0.0f;
    static float end_time   = 5.0f;

    const bool time_changed = ImGui::SliderFloat("Time##animation-time", &time, start_time, end_time);

    if (!time_changed) {
        return;
    }

    animation.apply(time);
    m_context.editor_message_bus->send_message(
        Editor_message{
            .update_flags = Message_flag_bit::c_flag_bit_animation_update
        }
    );

    // TODO assert all animation channels targets point to same scene?
    animation.channels.front().target->get_scene()->update_node_transforms();
}

void Properties::camera_properties(erhe::scene::Camera& camera) const
{
    ERHE_PROFILE_FUNCTION();

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    auto* const projection = camera.projection();
    if (
        (projection != nullptr) &&
        ImGui::TreeNodeEx(
            "Projection",
            ImGuiTreeNodeFlags_DefaultOpen
        )
    ) {
        ImGui::Indent(indent);
        ImGui::SetNextItemWidth(200);
        erhe::imgui::make_combo(
            "Type",
            projection->projection_type,
            erhe::scene::Projection::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings)
        );
        switch (projection->projection_type) {
            //using enum erhe::scene::Projection::Type;
            case erhe::scene::Projection::Type::perspective: {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_xr:{
                ImGui::SliderFloat("Fov Left",  &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Right", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Up",    &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Down",  &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Z Near",    &projection->z_near,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",     &projection->z_far,     0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_horizontal: {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::perspective_vertical: {
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_horizontal: {
                ImGui::SliderFloat("Width",  &projection->ortho_width, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,      0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,       0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_vertical: {
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal: {
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_rectangle: {
                ImGui::SliderFloat("Left",   &projection->ortho_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::generic_frustum: {
                ImGui::SliderFloat("Left",   &projection->frustum_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Right",  &projection->frustum_right,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Top",    &projection->frustum_top,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,         0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,          0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::other: {
                // TODO(tksuoran@gmail.com): Implement
                break;
            }
        }
        ImGui::Unindent(indent);
        ImGui::TreePop();
    }

    float exposure = camera.get_exposure();
    const auto avail = ImGui::GetContentRegionAvail().x;
    const auto label_size = std::max(
        ImGui::CalcTextSize("Exposure").x,
        ImGui::CalcTextSize("Shadow Range").x
    );
    const auto width = avail - label_size;
    {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 2000.0f, "%.3f", logarithmic)) {
            camera.set_exposure(exposure);
        }
    }
    float shadow_range = camera.get_shadow_range();
    {
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("Shadow Range", &shadow_range, 1.00f, 1000.0f, "%.3f", logarithmic)) {
            camera.set_shadow_range(shadow_range);
        }
    }
}

void Properties::light_properties(erhe::scene::Light& light) const
{
    ERHE_PROFILE_FUNCTION();

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    erhe::imgui::make_combo(
        "Type",
        light.type,
        erhe::scene::Light::c_type_strings,
        IM_ARRAYSIZE(erhe::scene::Light::c_type_strings)
    );
    if (light.type == erhe::scene::Light::Type::spot) {
        ImGui::SliderFloat("Inner Spot", &light.inner_spot_angle, 0.0f, glm::pi<float>());
        ImGui::SliderFloat("Outer Spot", &light.outer_spot_angle, 0.0f, glm::pi<float>());
    }
    ImGui::SliderFloat("Range",     &light.range,     1.00f, 20000.0f, "%.3f", logarithmic);
    ImGui::SliderFloat("Intensity", &light.intensity, 0.01f, 20000.0f, "%.3f", logarithmic);
    ImGui::ColorEdit3 ("Color",     &light.color.x,   ImGuiColorEditFlags_Float);

    const auto* node = light.get_node();
    if (node != nullptr) {
        auto* scene_root = static_cast<Scene_root*>(node->get_item_host());
        if (scene_root != nullptr) {
            const auto& layers = scene_root->layers();
            ImGui::ColorEdit3(
                "Ambient",
                &layers.light()->ambient_light.x,
                ImGuiColorEditFlags_Float
            );
        }
    }
}

void Properties::skin_properties(erhe::scene::Skin& skin) const
{
    ERHE_PROFILE_FUNCTION();

    auto& skin_data = skin.skin_data;
    ImGui::Text("Joint Count : %d", static_cast<int>(skin_data.joints.size()));
    if (!ImGui::TreeNodeEx("Skin")) {
        return;
    }
    for (auto& joint : skin_data.joints) {
        if (!joint) {
            ImGui::TextUnformatted("(missing joint)");
        } else {
            ImGui::TextUnformatted(joint->get_name().c_str());
        }
    }
    ImGui::TreePop();
}

auto layer_name(const erhe::scene::Layer_id layer_id) -> const char*
{
    switch (layer_id) {
        case Mesh_layer_id::brush       : return "brush";
        case Mesh_layer_id::content     : return "content";
        case Mesh_layer_id::sky         : return "sky";
        case Mesh_layer_id::controller  : return "controller";
        case Mesh_layer_id::tool        : return "tool";
        case Mesh_layer_id::rendertarget: return "rendertarget";
        default:                          return "?";
    }
}

void Properties::mesh_properties(erhe::scene::Mesh& mesh) const
{
    ERHE_PROFILE_FUNCTION();

    //ERHE_VERIFY(mesh.node_data.host != nullptr);

    auto& mesh_data  = mesh.mesh_data;
    const auto* node = mesh.get_node();
    auto* scene_root = static_cast<Scene_root*>(node->get_item_host());
    if (scene_root == nullptr) {
        ImGui::Text("Mesh host not set");
        return;
    }
    auto& material_library = scene_root->content_library()->materials;
    ImGui::Text("Layer ID: %u %s", static_cast<unsigned int>(mesh_data.layer_id), layer_name(mesh_data.layer_id));

    if (mesh_data.skin) {
        skin_properties(*mesh_data.skin.get());
    }

    int primitive_index = 0;
    for (auto& primitive : mesh_data.primitives) {
        const auto& geometry_primitive = primitive.geometry_primitive;
        if (!geometry_primitive) {
            continue;
        }
        const auto& geometry = geometry_primitive->source_geometry;

        ++primitive_index;
        ImGui::PushID(primitive_index);
        const std::string label = geometry
            ? fmt::format("Primitive: {}", geometry->name)
            : fmt::format("Primitive: {}", primitive_index);

        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(indent);
            material_library->combo(m_context, "Material", primitive.material, false);
            if (primitive.material) {
                ImGui::Text("Material Buffer Index: %u", primitive.material->material_buffer_index);
            } else {
                ImGui::Text("Null material");
            }
            if (geometry && ImGui::TreeNodeEx("Statistics")) {
                ImGui::Indent(indent);
                int point_count   = geometry->get_point_count();
                int polygon_count = geometry->get_polygon_count();
                int edge_count    = geometry->get_edge_count();
                int corner_count  = geometry->get_corner_count();
                ImGui::InputInt("Points",   &point_count,   0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Polygons", &polygon_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Edges",    &edge_count,    0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Corners",  &corner_count,  0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::Unindent(indent);
                ImGui::TreePop();
            }
            if (ImGui::TreeNodeEx("Debug")) {
                float bbox_volume    = geometry_primitive->gl_geometry_mesh.bounding_box.volume();
                float bsphere_volume = geometry_primitive->gl_geometry_mesh.bounding_sphere.volume();
                ImGui::Indent(indent);
                ImGui::InputFloat("BBox Volume",    &bbox_volume,    0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                ImGui::InputFloat("BSphere Volume", &bsphere_volume, 0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                ImGui::Unindent(indent);
                ImGui::TreePop();
            }
            ImGui::Unindent(indent);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void Properties::rendertarget_properties(Rendertarget_mesh& rendertarget) const
{
    ImGui::Text("Width: %f", rendertarget.width());
    ImGui::Text("Height: %f", rendertarget.height());
    ImGui::Text("Pixels per Meter: %f", static_cast<float>(rendertarget.pixels_per_meter()));
}

#endif

void Properties::on_begin()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
#endif
}

void Properties::on_end()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ImGui::PopStyleVar();
#endif
}

namespace {

[[nodiscard]] auto test_bits(uint64_t mask, uint64_t test_bits) -> bool
{
    return (mask & test_bits) == test_bits;
}

}

void Properties::node_physics_properties(Node_physics& node_physics) const
{
    erhe::physics::IRigid_body* rigid_body = node_physics.get_rigid_body();
    if (rigid_body == nullptr) {
        return;
    }

    const auto transform = rigid_body->get_world_transform();
    const glm::vec3 pos = glm::vec3{transform * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};

    ImGui::Text("Rigid Body: %s",             rigid_body->get_debug_label());
    ImGui::Text("Position: %.2f, %.2f, %.2f", pos.x, pos.y, pos.z);
    ImGui::Text("Is Active: %s",              rigid_body->is_active() ? "Yes" : "No");

    //bool allow_sleeping = rigid_body->get_allow_sleeping();
    //ImGui::Text("Allow Sleeping: %s", allow_sleeping ? "Yes" : "No");
    //if (allow_sleeping && ImGui::Button("Disallow Sleeping")) {
    //    rigid_body->set_allow_sleeping(false);
    //}
    //if (!allow_sleeping && ImGui::Button("Allow Sleeping")) {
    //    rigid_body->set_allow_sleeping(true);
    //}

    const auto collision_shape = rigid_body->get_collision_shape();
    if (collision_shape) {
        const auto com = collision_shape->get_center_of_mass();
        ImGui::TextUnformatted(collision_shape->describe().c_str());
        ImGui::Text("Local center of mass: %.2f, %.2f, %.2f", com.x, com.y, com.z);
    }

    float           mass    = rigid_body->get_mass();
    const glm::mat4 inertia = rigid_body->get_local_inertia();
    if (ImGui::SliderFloat("Mass", &mass, 0.01f, 1000.0f)) {
        rigid_body->set_mass_properties(mass, inertia);
    }
    float friction = rigid_body->get_friction();
    if (ImGui::SliderFloat("Friction", &friction, 0.0f, 1.0f)) {
        rigid_body->set_friction(friction);
    }
    float restitution = rigid_body->get_restitution();
    if (ImGui::SliderFloat("Restitution", &restitution, 0.0f, 1.0f)) {
        rigid_body->set_restitution(restitution);
    }

    // Static bodies don't have access to these properties, at least in Jolt
    if (rigid_body->get_motion_mode() != erhe::physics::Motion_mode::e_static) {
        float angular_damping = rigid_body->get_angular_damping();
        float linear_damping = rigid_body->get_linear_damping();
        if (
            ImGui::SliderFloat("Angular Dampening", &angular_damping, 0.0f, 1.0f) ||
            ImGui::SliderFloat("Linear Dampening", &linear_damping, 0.0f, 1.0f)
        ) {
            rigid_body->set_damping(linear_damping, angular_damping);
        }

        {
            const glm::mat4 local_inertia = rigid_body->get_local_inertia();
            float floats[4] = { local_inertia[0][0], local_inertia[1][1], local_inertia[2][2] };
            ImGui::InputFloat3("Local Inertia", floats);
            // TODO floats back to rigid body?
        }
    }

    const auto motion_mode = rigid_body->get_motion_mode();
    int i_motion_mode = static_cast<int>(motion_mode);
    if (
        ImGui::Combo(
            "Motion Mode",
            &i_motion_mode,
            erhe::physics::c_motion_mode_strings,
            IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings)
        )
    ) {
        rigid_body->set_motion_mode(static_cast<erhe::physics::Motion_mode>(i_motion_mode));
    }
}

void Properties::node_raytrace_properties(Node_raytrace& node_raytrace) const
{
    node_raytrace.properties_imgui();
}

void Properties::item_flags(const std::shared_ptr<erhe::Item>& item)
{
    if (!ImGui::TreeNodeEx("Flags")) {
        return;
    }

    ImGui::Indent(indent);

    using namespace erhe::bit;
    using Item_flags = erhe::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = uint64_t{1} << bit_position;
        bool           value    = test_all_rhs_bits_set(flags, bit_mask);
        if (ImGui::Checkbox(Item_flags::c_bit_labels[bit_position], &value)) {
            if (bit_mask == Item_flags::selected) {
                if (value) {
                    m_context.selection->add_to_selection(item);
                } else {
                    m_context.selection->remove_from_selection(item);
                }
            } else {
                item->set_flag_bits(bit_mask, value);
            }
        }
    }

    ImGui::Unindent(indent);

    ImGui::TreePop();
}

[[nodiscard]] auto show_item_details(const erhe::Item* const item)
{
    return
        !is_physics         (item) &&
        !is_frame_controller(item) &&
        !is_raytrace        (item) &&
        !is_rendertarget    (item);
}

void Properties::item_properties(const std::shared_ptr<erhe::Item>& item)
{
    if (is_raytrace(item)) { // currently hidden from user
        return;
    }

    const auto content_library_node = as<Content_library_node>(item);
    const auto node_physics         = as<Node_physics        >(item);
    const auto node_raytrace        = as<Node_raytrace       >(item);
    const auto rendertarget         = as<Rendertarget_mesh   >(item);
    const auto camera               = as<erhe::scene::Camera >(item);
    const auto light                = as<erhe::scene::Light  >(item);
    const auto mesh                 = as<erhe::scene::Mesh   >(item);
    const auto node                 = as<erhe::scene::Node   >(item);

    const bool default_open = !node_physics && !content_library_node && !node;

    if (
        !ImGui::TreeNodeEx(
            item->get_type_name().data(),
            ImGuiTreeNodeFlags_Framed |
            (default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0)
        )
    ) {
        return;
    }

    ImGui::PushID(item->get_label().c_str());

    if (show_item_details(item.get())) {
        std::string name = item->get_name();
        if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (name != item->get_name()) {
                item->set_name(name);
            }
        }

        ImGui::Text("Id: %u", static_cast<unsigned int>(item->get_id()));

        item_flags(item);

        if (!erhe::scene::is_light(item)) { // light uses light color, so hide item color
            glm::vec4 color = item->get_wireframe_color();
            if (
                ImGui::ColorEdit4(
                    "Item Color",
                    &color.x,
                    ImGuiColorEditFlags_Float |
                    ImGuiColorEditFlags_NoInputs
                )
            ) {
                item->set_wireframe_color(color);
            }
        }
    }

    if (node_physics) {
        node_physics_properties(*node_physics);
    }

    if (node_raytrace) {
        node_raytrace_properties(*node_raytrace);
    }

    if (camera) {
        camera_properties(*camera);
    }

    if (light) {
        light_properties(*light);
    }

    if (mesh) {
        mesh_properties(*mesh);
    }

    if (rendertarget) {
        rendertarget_properties(*rendertarget);
    }

    ImGui::TreePop();
    ImGui::PopID();
}

void Properties::material_properties()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const auto selected_material = m_context.selection->get<erhe::primitive::Material>();
    if (selected_material) {
        if (
            ImGui::TreeNodeEx(
                "Material",
                ImGuiTreeNodeFlags_Framed |
                ImGuiTreeNodeFlags_DefaultOpen
            )
        ) {
            std::string name = selected_material->get_name();
            if (ImGui::InputText("Name", &name)) {
                selected_material->set_name(name);
            }
            const auto  available_size = ImGui::GetContentRegionAvail();
            const float area_size_0    = std::min(available_size.x, available_size.y);
            const int   area_size      = std::max(1, static_cast<int>(area_size_0));
            m_context.material_preview->set_area_size(area_size);
            m_context.material_preview->update_rendertarget(*m_context.graphics_instance);
            m_context.material_preview->render_preview(selected_material);
            m_context.material_preview->show_preview();
            if (ImGui::TreeNodeEx("BRDF Slice", ImGuiTreeNodeFlags_None)) {
                auto* node = m_context.brdf_slice->get_node();
                if (node != nullptr) {
                    node->set_material(selected_material);
                    m_context.brdf_slice->show_brdf_slice(area_size);
                }
                ImGui::TreePop();
            }
            ImGui::SliderFloat("Metallic",    &selected_material->metallic,     0.0f,  1.0f);
            ImGui::SliderFloat("Reflectance", &selected_material->reflectance,  0.35f, 1.0f);
            ImGui::SliderFloat("Roughness X", &selected_material->roughness.x,  0.1f,  0.8f);
            ImGui::SliderFloat("Roughness Y", &selected_material->roughness.y,  0.1f,  0.8f);
            ImGui::ColorEdit4 ("Base Color",  &selected_material->base_color.x, ImGuiColorEditFlags_Float);
            ImGui::ColorEdit4 ("Emissive",    &selected_material->emissive.x,   ImGuiColorEditFlags_Float);
            ImGui::SliderFloat("Opacity",     &selected_material->opacity,      0.0f,  1.0f);
            ImGui::TreePop();
        }
    }
#endif
}

void Properties::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    ERHE_PROFILE_FUNCTION();

    const auto& selection = m_context.selection->get_selection();
    for (const auto& item : selection) {
        ERHE_VERIFY(item);
        item_properties(item);

        const auto node = as<erhe::scene::Node>(item);
        if (!node) {
            continue;
        }

        for (auto& attachment : node->get_attachments()) {
            item_properties(attachment);
        }
    }

    const auto selected_animation = m_context.selection->get<erhe::scene::Animation>();
    if (selected_animation) {
        animation_properties(*selected_animation.get());
    }

    const auto selected_skin = m_context.selection->get<erhe::scene::Skin>();
    if (selected_skin) {
        skin_properties(*selected_skin.get());
    }

    material_properties();
#endif
}

} // namespace editor
