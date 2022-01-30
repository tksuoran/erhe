#include "windows/node_properties.hpp"
#include "editor_imgui_windows.hpp"
#include "imgui_helpers.hpp"

#include "operations/insert_operation.hpp"
#include "operations/operation_stack.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_root.hpp"
#include "windows/log_window.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"
#include "erhe/scene/scene.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>
#include <imgui.h>
#include <imgui/misc/cpp/imgui_stdlib.h>

namespace editor
{

Node_properties::Node_properties()
    : erhe::components::Component{c_name}
    , Imgui_window               {c_title}
{
}

Node_properties::~Node_properties() = default;

void Node_properties::connect()
{
    m_operation_stack = get<Operation_stack>();
    m_scene_root      = get<Scene_root     >();
    m_selection_tool  = get<Selection_tool >();
    require<Editor_imgui_windows>();
}

void Node_properties::initialize_component()
{
    get<Editor_imgui_windows>()->register_imgui_window(this);
}

void Node_properties::icamera_properties(erhe::scene::ICamera& camera) const
{
    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    auto* const projection = camera.projection();
    if (projection == nullptr)
    {
        return;
    }

    ImGui::PushID("##icamera_properties");
    if (ImGui::TreeNodeEx("Projection", ImGuiTreeNodeFlags_Framed))
    {
        ImGui::SetNextItemWidth(200);
        make_combo(
            "Type",
            projection->projection_type,
            erhe::scene::Projection::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings)
        );
        switch (projection->projection_type)
        {
            using enum erhe::scene::Projection::Type;
            case perspective:
            {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case perspective_xr:
            {
                ImGui::SliderFloat("Fov Left",  &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Right", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Up",    &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Fov Down",  &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
                ImGui::SliderFloat("Z Near",    &projection->z_near,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",     &projection->z_far,     0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case perspective_horizontal:
            {
                ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case perspective_vertical:
            {
                ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case orthogonal_horizontal:
            {
                ImGui::SliderFloat("Width",  &projection->ortho_width, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,      0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,       0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case orthogonal_vertical:
            {
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case orthogonal:
            {
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case orthogonal_rectangle:
            {
                ImGui::SliderFloat("Left",   &projection->ortho_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case generic_frustum:
            {
                ImGui::SliderFloat("Left",   &projection->frustum_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Right",  &projection->frustum_right,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Top",    &projection->frustum_top,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near,         0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,          0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case other:
            {
                // TODO(tksuoran@gmail.com): Implement
                break;
            }
        }
        ImGui::TreePop();
    }
    if (ImGui::TreeNodeEx("Camera", ImGuiTreeNodeFlags_Framed))
    {
        float exposure = camera.get_exposure();
        if (ImGui::SliderFloat("Exposure", &exposure, 0.0f, 1000.0f, "%.3f", logarithmic))
        {
            camera.set_exposure(exposure);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Node_properties::light_properties(erhe::scene::Light& light) const
{
    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    ImGui::PushID("##light_properties");
    if (ImGui::TreeNodeEx("Light", ImGuiTreeNodeFlags_Framed))
    {
        make_combo(
            "Type",
            light.type,
            erhe::scene::Light::c_type_strings,
            IM_ARRAYSIZE(erhe::scene::Light::c_type_strings)
        );
        if (light.type == erhe::scene::Light::Type::spot)
        {
            ImGui::SliderFloat("Inner Spot", &light.inner_spot_angle, 0.0f, glm::pi<float>());
            ImGui::SliderFloat("Outer Spot", &light.outer_spot_angle, 0.0f, glm::pi<float>());
        }
        ImGui::SliderFloat("Range",     &light.range,     1.00f, 2000.0f, "%.3f", logarithmic);
        ImGui::SliderFloat("Intensity", &light.intensity, 0.01f, 2000.0f, "%.3f", logarithmic);
        ImGui::ColorEdit3 ("Color",     &light.color.x,   ImGuiColorEditFlags_Float);

        if (m_scene_root != nullptr)
        {
            ImGui::ColorEdit3(
                "Ambient",
                &m_scene_root->light_layer()->ambient_light.x,
                ImGuiColorEditFlags_Float
            );
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Node_properties::mesh_properties(erhe::scene::Mesh& mesh) const
{
    auto& mesh_data = mesh.data;

    ImGui::PushID("##mesh_properties");
    if (ImGui::TreeNodeEx("Mesh", ImGuiTreeNodeFlags_Framed))
    {
        ImGui::ColorEdit3("Wireframe Color", &mesh_data.wireframe_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);

        for (const auto& primitive : mesh_data.primitives)
        {
            const auto geometry = primitive.source_geometry;
            if (geometry)
            {
                if (ImGui::TreeNode(geometry->name.c_str()))
                {
                    int point_count   = geometry->get_point_count();
                    int polygon_count = geometry->get_polygon_count();
                    int edge_count    = geometry->get_edge_count();
                    int corner_count  = geometry->get_corner_count();
                    ImGui::InputInt("Points",   &point_count,   0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Polygons", &polygon_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Edges",    &edge_count,    0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::InputInt("Corners",  &corner_count,  0, 0, ImGuiInputTextFlags_ReadOnly);
                    ImGui::TreePop();
                }
            }
            ImGui::InputText("Material", &primitive.material->name, ImGuiInputTextFlags_ReadOnly);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

auto Node_properties::make_scalar_button(
    float* const      value,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) const -> Value_edit_state
{
    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    ImGui::SetNextItemWidth(12.0f);
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    constexpr float value_speed = 0.02f;
    return Value_edit_state{
        .value_changed = ImGui::DragFloat(imgui_label, value, value_speed),
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit()
    };
};

auto Node_properties::make_angle_button(
    float&            radians_value,
    const uint32_t    text_color,
    const uint32_t    background_color,
    const char* const label,
    const char* const imgui_label
) const -> Value_edit_state
{
    ImGui::PushStyleColor  (ImGuiCol_Text,          text_color);
    ImGui::PushStyleColor  (ImGuiCol_Button,        background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonHovered, background_color);
    ImGui::PushStyleColor  (ImGuiCol_ButtonActive,  background_color);
    ImGui::SetNextItemWidth(12.0f);
    ImGui::Button          (label);
    ImGui::SameLine        ();
    ImGui::PopStyleColor   (4);
    ImGui::SetNextItemWidth(100.0f);

    float degrees_value = glm::degrees<float>(radians_value);
    constexpr float value_speed = 1.0f;
    constexpr float value_min   = -360.0f;
    constexpr float value_max   =  360.0f;

    const bool value_changed = ImGui::DragFloat(
        imgui_label,
        &degrees_value,
        value_speed,
        value_min,
        value_max,
        "%.f\xc2\xb0" // \xc2\xb0 is degree symbol UTF-8 encoded
    );
    if (value_changed)
    {
        radians_value = glm::radians<float>(degrees_value);
    }
    return Value_edit_state{
        .value_changed = value_changed,
        .edit_ended    = ImGui::IsItemDeactivatedAfterEdit()
    };
};

Node_properties::Node_state::Node_state(erhe::scene::Node& node)
    : node                              {&node}
    , touched                           {false}
    , initial_parent_from_node_transform{node.parent_from_node_transform()}
    , initial_world_from_node_transform {node.world_from_node_transform()}
{
}

auto Node_properties::get_node_state(erhe::scene::Node& node) -> Node_state&
{
    for (auto& entry : m_node_states)
    {
        if (entry.node == &node)
        {
            return entry;
        }
    }
    return m_node_states.emplace_back(node);
}

auto Node_properties::drop_node_state(erhe::scene::Node& node)
{
    std::erase_if(
        m_node_states,
        [&node](auto& entry) -> bool
        {
            return entry.node == &node;
        }
    );
}

void Node_properties::transform_properties(erhe::scene::Node& node)
{
    ImGui::PushID("##transform_properties");

    if (ImGui::TreeNodeEx("Transform", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
    {
        auto& node_state = get_node_state(node);

        const glm::mat4 transform = node.world_from_node();
        glm::vec3 scale;
        glm::quat orientation;
        glm::vec3 translation;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(transform, scale, orientation, translation, skew, perspective);

        const glm::mat4 orientation_matrix(orientation);
        glm::vec3 euler_angles;
        glm::extractEulerAngleZYX(orientation_matrix, euler_angles.x, euler_angles.y, euler_angles.z);

        //if (ImGui::TreeNodeEx("Matrix", ImGuiTreeNodeFlags_SpanFullWidth))
        //{
        //    for (int row = 0; row < 4; row++)
        //    {
        //        const std::string label     = fmt::format("M{}", row);
        //        glm::vec4         rowVector = glm::vec4{transform[0][row], transform[1][row], transform[2][row], transform[3][row]};
        //        ImGui::InputFloat4(label.c_str(), &rowVector.x, "%.3f");
        //        // TODO rowVector back to matrix?
        //    }
        //    ImGui::TreePop();
        //}

        Value_edit_state edit_state;
        if (ImGui::TreeNodeEx("Translation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            edit_state.combine(make_scalar_button(&translation.x, 0xff8888ffu, 0xff222266u, "X", "##T.X"));
            edit_state.combine(make_scalar_button(&translation.y, 0xff88ff88u, 0xff226622u, "Y", "##T.Y"));
            edit_state.combine(make_scalar_button(&translation.z, 0xffff8888u, 0xff662222u, "Z", "##T.Z"));
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Rotation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            edit_state.combine(make_angle_button(euler_angles.x, 0xff8888ffu, 0xff222266u, "X", "##R.X"));
            edit_state.combine(make_angle_button(euler_angles.y, 0xff88ff88u, 0xff226622u, "Y", "##R.Y"));
            edit_state.combine(make_angle_button(euler_angles.z, 0xffff8888u, 0xff662222u, "Z", "##R.Z"));
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Scale"))
        {
            edit_state.combine(make_scalar_button(&scale.x, 0xff8888ffu, 0xff222266u, "X", "##S.X"));
            edit_state.combine(make_scalar_button(&scale.y, 0xff88ff88u, 0xff226622u, "Y", "##S.Y"));
            edit_state.combine(make_scalar_button(&scale.z, 0xffff8888u, 0xff662222u, "Z", "##S.Z"));
            ImGui::TreePop();
        }

        erhe::scene::Transform new_parent_from_node;
        if (edit_state.value_changed || (edit_state.edit_ended && node_state.touched))
        {
            node_state.touched = true;
            const glm::mat4 new_translation     = erhe::toolkit::create_translation<float>(translation);
            const glm::mat4 new_rotation        = glm::eulerAngleZYX(euler_angles.x, euler_angles.y, euler_angles.z);
            const glm::mat4 new_scale           = erhe::toolkit::create_scale<float>(scale);
            const glm::mat4 new_world_from_node = new_translation * new_rotation * new_scale;
            if (node.parent() == nullptr)
            {
                new_parent_from_node = erhe::scene::Transform{new_world_from_node};
            }
            else
            {
                new_parent_from_node = node.parent()->node_from_world_transform() * erhe::scene::Transform{new_world_from_node};
            }
            if (!edit_state.edit_ended)
            {
                node.set_parent_from_node(new_parent_from_node);
            }
        }

        if (edit_state.edit_ended && node_state.touched)
        {
            m_operation_stack->push(
                std::make_shared<Node_transform_operation>(
                    Node_transform_operation::Context{
                        .layer                   = *m_scene_root->content_layer(),
                        .scene                   = m_scene_root->scene(),
                        .physics_world           = m_scene_root->physics_world(),
                        .node                    = node.shared_from_this(),
                        .parent_from_node_before = node_state.initial_parent_from_node_transform,
                        .parent_from_node_after  = new_parent_from_node//m_target_node->parent_from_node_transform()
                    }
                )
            );
            node_state.touched = false;
        }

        if (!node_state.touched)
        {
            drop_node_state(node);
        }

        ImGui::TreePop();
    }
    ImGui::PopID();
}

void Node_properties::on_begin()
{
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 0.0f);
}

void Node_properties::on_end()
{
    ImGui::PopStyleVar();
}

void Node_properties::imgui()
{
    if (!m_selection_tool)
    {
        return;
    }

    const auto& selection = m_selection_tool->selection();
    for (const auto& node : selection)
    {
        if (!node)
        {
            continue;
        }

        std::string name = node->name();
        if (ImGui::InputText("Name", &name, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (name != node->name())
            {
                node->set_name(name);
            }
        }

        const auto& icamera = as_icamera(node);
        if (icamera)
        {
            icamera_properties(*icamera.get());
        }

        const auto& light = as_light(node);
        if (light)
        {
            light_properties(*light.get());
        }

        const auto& mesh = as_mesh(node);
        if (mesh)
        {
            mesh_properties(*mesh.get());
        }

        transform_properties(*node.get());
    }
}

}
