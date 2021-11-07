#include "windows/camera_properties.hpp"
#include "tools.hpp"
#include "scene/scene_manager.hpp"
#include "scene/scene_root.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/scene/camera.hpp"

#include "imgui.h"

namespace editor
{

Camera_properties::Camera_properties()
    : erhe::components::Component{c_name}
{
}

Camera_properties::~Camera_properties() = default;

void Camera_properties::connect()
{
    m_scene_root     = get<Scene_root    >();
    m_selection_tool = get<Selection_tool>();
}

void Camera_properties::initialize_component()
{
    get<Editor_tools>()->register_imgui_window(this);
}

void Camera_properties::imgui(Pointer_context& /*pointer_context*/)
{
    ImGui::Begin("Camera");

    //for (auto item : m_selection_tool->selection())
    {
        //auto camera = dynamic_pointer_cast<erhe::scene::Camera>(item);
        auto* const camera = get<Scene_manager>()->get_view_camera().get();
        if (!camera)
        {
            //continue;
            return;
        }
        auto* const projection = camera->projection();
        if (projection != nullptr)
        {
            const ImGuiSliderFlags logarithmic = ImGuiSliderFlags_Logarithmic;
            ImGui::Text("%s", camera->name().c_str());
            ImGui::Separator();
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
    }

    ImGui::End();
}

} // namespace editor
