#include "windows/camera_properties.hpp"
#include "scene/scene_manager.hpp"
#include "erhe/scene/camera.hpp"

#include "imgui.h"

namespace editor
{

Camera_properties::Camera_properties(const std::shared_ptr<Scene_manager>& scene_manager)
    : m_scene_manager(scene_manager)
{
}

void Camera_properties::window(Pointer_context&)
{
    ImGui::Begin("Camera");

    auto& camera = m_scene_manager->camera();
    auto* projection = camera.projection();
    if (projection != nullptr)
    {
        const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
        ImGui::Text("%s", camera.node()->name.c_str());
        ImGui::Separator();
        ImGui::SetNextItemWidth(200);
        int type = static_cast<int>(projection->projection_type);
        ImGui::BeginGroup();
        ImGui::Combo("Type", &type, erhe::scene::Projection::c_type_strings, IM_ARRAYSIZE(erhe::scene::Projection::c_type_strings));
        projection->projection_type = static_cast<erhe::scene::Projection::Type>(type);
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
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_vertical:
            {
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal:
            {
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::orthogonal_rectangle:
            {
                ImGui::SliderFloat("Left",   &projection->ortho_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Width",  &projection->ortho_width,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->ortho_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Height", &projection->ortho_height, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::generic_frustum:
            {
                ImGui::SliderFloat("Left",   &projection->frustum_left,   0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Right",  &projection->frustum_right,  0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Bottom", &projection->frustum_bottom, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Top",    &projection->frustum_top,    0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Near", &projection->z_near, 0.0f, 1000.0f, "%.3f", logarithmic);
                ImGui::SliderFloat("Z Far",  &projection->z_far,  0.0f, 1000.0f, "%.3f", logarithmic);
                break;
            }

            case erhe::scene::Projection::Type::other:
                // TODO(tksuoran@gmail.com): Implement
                break;
        }
        ImGui::EndGroup();
    }

    ImGui::End();
}

} // namespace editor
