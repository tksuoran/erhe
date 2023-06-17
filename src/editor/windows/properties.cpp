#include "windows/properties.hpp"

#include "editor_log.hpp"
#include "editor_scenes.hpp"
#include "rendertarget_mesh.hpp"
#include "operations/node_operation.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"
#include "scene/frame_controller.hpp"
#include "scene/material_library.hpp"
#include "scene/material_preview.hpp"
#include "scene/node_physics.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "windows/content_library_window.hpp"

#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/windows/log_window.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/scene/skin.hpp"
#include "erhe/toolkit/bit_helpers.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#   include <imgui_internal.h>
#   include <imgui/misc/cpp/imgui_stdlib.h>
#endif

namespace editor
{

const float indent = 15.0f;

Properties* g_properties{nullptr};

Properties::Properties()
    : erhe::components::Component{c_type_name}
    , Imgui_window               {c_title}
{
}

Properties::~Properties() noexcept
{
    ERHE_VERIFY(g_properties == this);
    g_properties = nullptr;
}

void Properties::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
}

void Properties::initialize_component()
{
    ERHE_VERIFY(g_properties == nullptr);
    erhe::application::g_imgui_windows->register_imgui_window(this, "properties");
    g_properties = this;
}

#if defined(ERHE_GUI_LIBRARY_IMGUI)
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
        erhe::application::make_combo(
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

    erhe::application::make_combo(
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
        auto* scene_root = reinterpret_cast<Scene_root*>(node->get_item_host());
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

    auto& skin_data  = skin.skin_data;
    const auto* node = skin.get_node();
    auto* scene_root = reinterpret_cast<Scene_root*>(node->get_item_host());
    if (scene_root == nullptr) {
        ImGui::Text("Skin host not set");
        return;
    }
    if (!ImGui::TreeNodeEx("Skin", ImGuiTreeNodeFlags_DefaultOpen)) {
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

void Properties::mesh_properties(erhe::scene::Mesh& mesh) const
{
    ERHE_PROFILE_FUNCTION();

    //ERHE_VERIFY(mesh.node_data.host != nullptr);

    auto& mesh_data  = mesh.mesh_data;
    const auto* node = mesh.get_node();
    auto* scene_root = reinterpret_cast<Scene_root*>(node->get_item_host());
    if (scene_root == nullptr) {
        ImGui::Text("Mesh host not set");
        return;
    }
    auto& material_library = scene_root->content_library()->materials;

    int primitive_index = 0;
    for (auto& primitive : mesh_data.primitives) {
        const auto& geometry = primitive.source_geometry;

        ++primitive_index;
        const std::string label = geometry
            ? fmt::format("Primitive: {}", geometry->name)
            : fmt::format("Primitive: {}", primitive_index);

        if (ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Indent(indent);
            material_library.combo("Material", primitive.material, false);
            if (primitive.material) {
                ImGui::Text("Material Buffer Index: %u", primitive.material->material_buffer_index);
            } else {
                ImGui::Text("Null material");
            }
            if (
                geometry &&
                ImGui::TreeNodeEx("Statistics")
            ) {
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
                float bbox_volume    = primitive.gl_primitive_geometry.bounding_box.volume();
                float bsphere_volume = primitive.gl_primitive_geometry.bounding_sphere.volume();
                ImGui::Indent(indent);
                ImGui::InputFloat("BBox Volume",    &bbox_volume,    0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                ImGui::InputFloat("BSphere Volume", &bsphere_volume, 0, 0, "%.4f", ImGuiInputTextFlags_ReadOnly);
                ImGui::Unindent(indent);
                ImGui::TreePop();
            }
            ImGui::Unindent(indent);
            ImGui::TreePop();
        }
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
    erhe::physics::IRigid_body* rigid_body = node_physics.rigid_body();
    if (rigid_body == nullptr) {
        return;
    }

    ImGui::Text("Rigid Body: %s", rigid_body->get_debug_label());
    float           mass    = rigid_body->get_mass();
    const glm::mat4 inertia = rigid_body->get_local_inertia();
    if (ImGui::SliderFloat("Mass", &mass, 0.0f, 1000.0f)) {
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

    int motion_mode = static_cast<int>(rigid_body->get_motion_mode());
    if (
        ImGui::Combo(
            "Motion Mode",
            &motion_mode,
            erhe::physics::c_motion_mode_strings,
            IM_ARRAYSIZE(erhe::physics::c_motion_mode_strings)
        )
    ) {
        rigid_body->set_motion_mode(static_cast<erhe::physics::Motion_mode>(motion_mode));
    }
}

void Properties::item_flags(const std::shared_ptr<erhe::scene::Item>& item)
{
    if (!ImGui::TreeNodeEx("Flags")) {
        return;
    }

    ImGui::Indent(indent);

    using namespace erhe::toolkit;
    using Item_flags = erhe::scene::Item_flags;

    const uint64_t flags = item->get_flag_bits();
    for (uint64_t bit_position = 0; bit_position < Item_flags::count; ++ bit_position) {
        const uint64_t bit_mask = uint64_t{1} << bit_position;
        bool           value    = test_all_rhs_bits_set(flags, bit_mask);
        if (ImGui::Checkbox(Item_flags::c_bit_labels[bit_position], &value)) {
            if (bit_mask == Item_flags::selected) {
                if (value) {
                    g_selection_tool->add_to_selection(item);
                } else {
                    g_selection_tool->remove_from_selection(item);
                }
            } else {
                item->set_flag_bits(bit_mask, value);
            }
        }
    }

    ImGui::Unindent(indent);

    ImGui::TreePop();
}

[[nodiscard]] auto show_item_details(const erhe::scene::Item* const item)
{
    return
        !is_physics         (item) &&
        !is_frame_controller(item) &&
        !is_raytrace        (item) &&
        !is_rendertarget    (item);
}

void Properties::item_properties(const std::shared_ptr<erhe::scene::Item>& item)
{
    if (is_raytrace(item)) { // currently nothing to show, so hidden from user
        return;
    }

    const auto node_physics = as_physics     (item);
    const auto camera       = as_camera      (item);
    const auto light        = as_light       (item);
    const auto mesh         = as_mesh        (item);
    const auto skin         = as_skin        (item);
    const auto rendertarget = as_rendertarget(item);

    const bool default_open = !node_physics;

    if (
        !ImGui::TreeNodeEx(
            item->type_name(),
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

        if (!is_light(item)) { // light uses light color, so hide item color
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

    if (camera) {
        camera_properties(*camera);
    }

    if (light) {
        light_properties(*light);
    }

    if (mesh) {
        mesh_properties(*mesh);
    }

    if (skin) {
        skin_properties(*skin);
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
    const auto selected_material = g_content_library_window->selected_material();
    if (selected_material) {
        if (
            ImGui::TreeNodeEx(
                "Material",
                ImGuiTreeNodeFlags_Framed |
                0 //ImGuiTreeNodeFlags_DefaultOpen
            )
        ) {
            std::string name = selected_material->get_name();
            if (ImGui::InputText("Name", &name)) {
                selected_material->set_name(name);
            }
            g_material_preview->render_preview(selected_material);
            g_material_preview->show_preview();
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

    if (g_selection_tool == nullptr) {
        return;
    }

    const auto& selection = g_selection_tool->get_selection();
    for (const auto& item : selection) {
        ERHE_VERIFY(item);
        item_properties(item);
    }

    material_properties();
#endif
}

} // namespace editor
