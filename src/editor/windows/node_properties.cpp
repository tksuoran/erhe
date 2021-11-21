#include "windows/node_properties.hpp"
#include "tools.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"

#include "erhe/geometry/geometry.hpp"
#include "erhe/imgui/imgui_helpers.hpp"
#include "erhe/primitive/primitive.hpp"
#include "erhe/primitive/primitive_geometry.hpp"
#include "erhe/primitive/material.hpp"
#include "erhe/scene/camera.hpp"
#include "erhe/scene/light.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

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
    m_scene_manager  = get<Scene_manager>();
    m_selection_tool = get<Selection_tool>();
}

void Node_properties::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Node_properties::camera_properties(erhe::scene::Camera& camera) const
{
    using namespace erhe::imgui;

    auto* const projection = camera.projection();
    if (projection == nullptr)
    {
        return;
    }

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
    ImGui::SetNextItemWidth(200);
    make_combo("Type", projection->projection_type, erhe::scene::Projection::c_type_strings, IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings));
    switch (projection->projection_type)
    {
        case erhe::scene::Projection::Type::perspective:
        {
            ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
            ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
            ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::perspective_xr:
        {
            ImGui::SliderFloat("Fov Left",  &projection->fov_left,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
            ImGui::SliderFloat("Fov Right", &projection->fov_right, -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
            ImGui::SliderFloat("Fov Up",    &projection->fov_up,    -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
            ImGui::SliderFloat("Fov Down",  &projection->fov_down,  -glm::pi<float>() / 2.0f, glm::pi<float>() / 2.0f);
            ImGui::SliderFloat("Z Near",    &projection->z_near,    0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",     &projection->z_far,     0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::perspective_horizontal:
        {
            ImGui::SliderFloat("Fov X",  &projection->fov_x,  0.0f, glm::pi<float>());
            ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::perspective_vertical:
        {
            ImGui::SliderFloat("Fov Y",  &projection->fov_y,  0.0f, glm::pi<float>());
            ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::orthogonal_horizontal:
        {
            ImGui::SliderFloat("Width",  &projection->ortho_width, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Near", &projection->z_near,      0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,       0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::orthogonal_vertical:
        {
            ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::orthogonal:
        {
            ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::orthogonal_rectangle:
        {
            ImGui::SliderFloat("Left",   &projection->ortho_left,   0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Bottom", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Near", &projection->z_near,       0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,        0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::generic_frustum:
        {
            ImGui::SliderFloat("Left",   &projection->frustum_left,   0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Right",  &projection->frustum_right,  0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Bottom", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Top",    &projection->frustum_top,    0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Near", &projection->z_near,         0.0f, 1000.0f, "%.3f", logarithmic);
            ImGui::SliderFloat("Z Far",  &projection->z_far,          0.0f, 1000.0f, "%.3f", logarithmic);
            break;
        }

        case erhe::scene::Projection::Type::other:
            // TODO(tksuoran@gmail.com): Implement
            break;
    }
}

void Node_properties::light_properties(erhe::scene::Light& light) const
{
    using namespace erhe::imgui;

    const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;

    make_combo("Type", light.type, erhe::scene::Light::c_type_strings, IM_ARRAYSIZE(erhe::scene::Light::c_type_strings));
    if (light.type == erhe::scene::Light::Type::spot)
    {
        ImGui::SliderFloat("Inner Spot", &light.inner_spot_angle, 0.0f, glm::pi<float>());
        ImGui::SliderFloat("Outer Spot", &light.outer_spot_angle, 0.0f, glm::pi<float>());
    }
    ImGui::SliderFloat("Intensity", &light.intensity, 0.01f, 2000.0f, "%.3f", logarithmic);
    ImGui::ColorEdit3 ("Color",     &light.color.x,   ImGuiColorEditFlags_Float);
}

void Node_properties::mesh_properties(erhe::scene::Mesh& mesh) const
{
    auto& mesh_data = mesh.data;
    ImGui::ColorEdit3("Wireframe Color", &mesh_data.wireframe_color.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_NoInputs);

    for (const auto& primitive : mesh_data.primitives)
    {
        const auto geometry = primitive.primitive_geometry->source_geometry;
        if (geometry != nullptr)
        {
            if (ImGui::TreeNode(geometry->name.c_str()))
            {
                int point_count   = geometry->point_count();
                int polygon_count = geometry->polygon_count();
                int edge_count    = geometry->edge_count();
                int corner_count  = geometry->corner_count();
                ImGui::InputInt("Points",   &point_count,   0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Polygons", &polygon_count, 0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Edges",    &edge_count,    0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::InputInt("Corners",  &corner_count,  0, 0, ImGuiInputTextFlags_ReadOnly);
                ImGui::TreePop();
            }
        }
        ImGui::InputText("Material", &primitive.material->name, ImGuiInputTextFlags_ReadOnly);
    }
}

void Node_properties::transform_properties(erhe::scene::Node& node) const
{
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

    if (ImGui::TreeNode("Transform Matrix"))
    {
        for (int row = 0; row < 4; row++)
        {
            const std::string label     = fmt::format("M{}", row);
            glm::vec4         rowVector = glm::vec4{transform[0][row], transform[1][row], transform[2][row], transform[3][row]};
            ImGui::InputFloat4(label.c_str(), &rowVector.x, "%.3f");
            // TODO rowVector back to matrix?
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Translation"))
    {
        //float range_min = -20.0f;
        //float range_max =  20.0f;
        ImGui::InputFloat("T.X", &translation.x); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.x, range_min, range_max, "%.2f");
        ImGui::InputFloat("T.Y", &translation.y); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.y, range_min, range_max, "%.2f");
        ImGui::InputFloat("T.Z", &translation.z); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.z, range_min, range_max, "%.2f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Rotation"))
    {
        //float range_min = -glm::pi<float>();
        //float range_max =  glm::pi<float>();
        ImGui::InputFloat("R.X", &euler_angles.x); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.x, range_min, range_max, "%.2f");
        ImGui::InputFloat("R.Y", &euler_angles.y); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.y, range_min, range_max, "%.2f");
        ImGui::InputFloat("R.Z", &euler_angles.z); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.z, range_min, range_max, "%.2f");
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Scale"))
    {
        //float range_min = -10.0f;
        //float range_max =  10.0f;
        ImGui::InputFloat("X", &scale.x); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.x, range_min, range_max, "%.2f");
        ImGui::InputFloat("Y", &scale.y); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.y, range_min, range_max, "%.2f");
        ImGui::InputFloat("Z", &scale.z); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.z, range_min, range_max, "%.2f");
        ImGui::TreePop();
    }
}

void Node_properties::imgui(Pointer_context&)
{
    const auto& selection = m_selection_tool->selection();
    ImGui::Begin("Node");
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

        auto camera = as_camera(node);
        if (camera)
        {
            camera_properties(*camera.get());
        }

        auto light = as_light(node);
        if (light)
        {
            light_properties(*light.get());
        }
        auto mesh = as_mesh(node);
        if (mesh)
        {
            mesh_properties(*mesh.get());
        }

        transform_properties(*node.get());

        ImGui::Separator();
    }
    ImGui::End();
}

}
