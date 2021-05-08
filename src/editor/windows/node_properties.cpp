#include "windows/node_properties.hpp"
#include "tools/selection_tool.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/scene/mesh.hpp"
#include "erhe/scene/node.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/constants.hpp>

#include "imgui.h"

namespace editor
{

void Node_properties::connect()
{
    m_scene_manager  = get<Scene_manager>();
    m_selection_tool = get<Selection_tool>();
}

void Node_properties::window(Pointer_context&)
{
    const auto& selected_meshes = m_selection_tool->selected_meshes();
    ImGui::Begin("Node");
    for (const auto& mesh : selected_meshes)
    {
        if (!mesh)
        {
            continue;
        }
        ImGui::Text("Mesh: %s", mesh->name.c_str());
        auto* node = mesh->node.get();
        if (node != nullptr)
        {
            glm::mat4 transform = node->world_from_node();
            glm::vec3 scale;
            glm::quat orientation;
            glm::vec3 translation;
            glm::vec3 skew;
            glm::vec4 perspective;
            glm::decompose(transform, scale, orientation, translation, skew, perspective);
            glm::mat4 orientation_matrix(orientation);
            glm::vec3 euler_angles;
            glm::extractEulerAngleZYX(orientation_matrix, euler_angles.x, euler_angles.y, euler_angles.z);
            ImGui::Text("Node: %s", node->name.c_str());

            {
                ImGui::BeginGroup();
                ImGui::Text("Transform Matrix");
                for (int row = 0; row < 4; row++)
                {
                    std::string label = fmt::format("M{}", row);
                    glm::vec4 rowVector = glm::vec4(transform[0][row], transform[1][row], transform[2][row], transform[3][row]);
                    ImGui::InputFloat4(label.c_str(), &rowVector.x, "%.3f");
                    //for (int column = 0; column < 4; column++)
                    //{
                    //    std::string label = fmt::format("M{}{}", row, column);
                    //    ImGui::TableSetColumnIndex(column);
                    //    ImGui::InputFloat(label.c_str(), &transform[row][column], 0.0f, 0.0f, "%.2f", 0);
                    //}
                }
                ImGui::EndGroup();
            }

            {
                ImGui::BeginGroup();
                ImGui::Text("Translation");
                float range_min = -20.0f;
                float range_max =  20.0f;
                ImGui::InputFloat("T.X", &translation.x); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.x, range_min, range_max, "%.2f");
                ImGui::InputFloat("T.Y", &translation.y); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.y, range_min, range_max, "%.2f");
                ImGui::InputFloat("T.Z", &translation.z); //ImGui::SameLine(); ImGui::SliderFloat(nullptr, &translation.z, range_min, range_max, "%.2f");
                ImGui::EndGroup();
            }
            
            {
                ImGui::BeginGroup();
                ImGui::Text("Rotation");
                float range_min = -glm::pi<float>();
                float range_max =  glm::pi<float>();
                ImGui::InputFloat("R.X", &euler_angles.x); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.x, range_min, range_max, "%.2f");
                ImGui::InputFloat("R.Y", &euler_angles.y); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.y, range_min, range_max, "%.2f");
                ImGui::InputFloat("R.Z", &euler_angles.z); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &euler_angles.z, range_min, range_max, "%.2f");
                ImGui::EndGroup();
            }

            {
                ImGui::BeginGroup();
                ImGui::Text("Scale");
                float range_min = -10.0f;
                float range_max =  10.0f;
                ImGui::InputFloat("X", &scale.x); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.x, range_min, range_max, "%.2f");
                ImGui::InputFloat("Y", &scale.y); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.y, range_min, range_max, "%.2f");
                ImGui::InputFloat("Z", &scale.z); // ImGui::SameLine(); ImGui::SliderFloat(nullptr, &scale.z, range_min, range_max, "%.2f");
                ImGui::EndGroup();
            }
        }
        ImGui::Separator();
    }
    ImGui::End();
}

}
