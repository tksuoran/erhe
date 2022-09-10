#include "tools/hotbar.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/tools.hpp"
#include "rendertarget_node.hpp"
#include "rendertarget_imgui_viewport.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/scene/scene.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

Hotbar::Hotbar()
    : erhe::application::Imgui_window{c_title, c_type_name}
    , erhe::components::Component    {c_type_name}
{
}

Hotbar::~Hotbar() noexcept
{
}

void Hotbar::declare_required_components()
{
    require<erhe::application::Imgui_windows>();
    require<erhe::application::Rendergraph  >();
    require<Fly_camera_tool >();
    require<Scene_builder   >();
    require<Tools           >();
    require<Viewport_windows>();
}

void Hotbar::initialize_component()
{
    get<Tools                           >()->register_background_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    //const auto& rendergraph             = get<erhe::application::Rendergraph  >();
    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& scene_builder           = get<Scene_builder   >();
    //const auto& viewport_windows        = get<Viewport_windows>();
    const auto& primary_viewport_window = scene_builder->get_primary_viewport_window();
    const auto& scene_root              = scene_builder->get_scene_root();

    m_rendertarget_node = scene_root->create_rendertarget_node(
        *m_components,
        *primary_viewport_window.get(),
        512,
        64,
        2000.0
    );

    scene_root->scene().add_to_mesh_layer(
        *scene_root->layers().rendertarget(),
        m_rendertarget_node
    );

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_node.get(),
        "Hotbar Rendertarget ImGui Viewport1",
        *m_components
    );

    // Also registers rendertarget node
    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    set_viewport(m_rendertarget_imgui_viewport.get());
    show();

}

void Hotbar::post_initialize()
{
    m_viewport_windows = get<Viewport_windows>();
}

auto Hotbar::description() -> const char*
{
   return c_title.data();
}

void Hotbar::update_once_per_frame(
    const erhe::components::Time_context& /*time_context*/
)
{
    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return;
    }
    const auto camera = viewport_window->get_camera();
    //if (m_rendertarget_node->parent().lock() == camera)
    //{
    //    return;
    //}
    m_rendertarget_node->set_parent(camera);

    const glm::mat4 m = erhe::toolkit::create_look_at(
        glm::vec3{ m_x, m_y, m_z},
        glm::vec3{ 0.0f, 2.0f * -m_y, 0.0f},
        glm::vec3{ 0.0f, 1.0f, 0.0f}
    );

    m_rendertarget_node->set_parent_from_node(m);
    m_rendertarget_node->update_transform();
}

void Hotbar::tool_render(
    const Render_context& /*context*/
)
{
}

auto Hotbar::flags() -> ImGuiWindowFlags
{
    return
        ImGuiWindowFlags_NoTitleBar        |
        ImGuiWindowFlags_NoResize          |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_NoScrollbar       |
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoNavInputs       |
        ImGuiWindowFlags_NoNavFocus;
}

void Hotbar::on_begin()
{
    m_min_size[0] = static_cast<float>(m_rendertarget_node->width());
    m_min_size[1] = static_cast<float>(m_rendertarget_node->height());
    m_max_size[0] = m_min_size[0];
    m_max_size[1] = m_min_size[1];
    ImGui::SetNextWindowPos(ImVec2{0.0f, 0.0f});
}

void Hotbar::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    const ImVec2 button_size{64.0f, 64.0f};
    //ImGui::SliderFloat("X", &m_x, -2.0f, 2.0f);
    //ImGui::SliderFloat("Y", &m_y, -0.3f, 0.0f);
    //ImGui::SliderFloat("Z", &m_z, -2.0f, 2.0f);
    ImGui::Button("1", button_size); ImGui::SameLine();
    ImGui::Button("2", button_size); ImGui::SameLine();
    ImGui::Button("3", button_size); ImGui::SameLine();
    ImGui::Button("4", button_size); ImGui::SameLine();
    ImGui::Button("5", button_size); ImGui::SameLine();
    ImGui::Button("6", button_size); ImGui::SameLine();
    ImGui::Button("7", button_size); ImGui::SameLine();
    ImGui::Button("8", button_size); ImGui::SameLine();
    ImGui::Button("9", button_size); ImGui::SameLine();
#endif
}

} // namespace editor
