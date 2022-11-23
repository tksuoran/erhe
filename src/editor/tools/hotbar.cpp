#include "tools/hotbar.hpp"
#include "graphics/icon_set.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/fly_camera_tool.hpp"
#include "tools/physics_tool.hpp"
#include "tools/selection_tool.hpp"
#include "tools/trs_tool.hpp"
#include "tools/tools.hpp"
#include "rendertarget_node.hpp"
#include "rendertarget_imgui_viewport.hpp"
#if defined(ERHE_XR_LIBRARY_OPENXR)
#   include "xr/headset_view.hpp"
#endif
#include "windows/operations.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/graphics/gl_context_provider.hpp"
#include "erhe/application/imgui/imgui_helpers.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

#if defined(ERHE_GUI_LIBRARY_IMGUI)
#   include <imgui.h>
#endif

namespace editor
{

using glm::vec3;

Hotbar::Hotbar()
    : erhe::application::Imgui_window{c_title}
    , erhe::components::Component    {c_type_name}
{
}

Hotbar::~Hotbar() noexcept
{
}

void Hotbar::declare_required_components()
{
    require<erhe::application::Configuration      >();
    require<erhe::application::Gl_context_provider>();
    require<erhe::application::Imgui_windows      >();
    require<erhe::application::Rendergraph        >();
    require<Fly_camera_tool >();
    require<Scene_builder   >();
    require<Tools           >();
    require<Viewport_windows>();
}

void Hotbar::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto& configuration = get<erhe::application::Configuration>();
    const auto& hotbar        = configuration->hotbar;
    if (!hotbar.enabled)
    {
        return;
    }

    const erhe::application::Scoped_gl_context gl_context{
        get<erhe::application::Gl_context_provider>()
    };

    get<Tools                           >()->register_background_tool(this);
    get<erhe::application::Imgui_windows>()->register_imgui_window(this);

    m_show = hotbar.show;
    m_x    = hotbar.x;
    m_y    = hotbar.y;
    m_z    = hotbar.z;

    const auto& imgui_windows           = get<erhe::application::Imgui_windows>();
    const auto& scene_builder           = get<Scene_builder   >();
    const auto& primary_viewport_window = scene_builder->get_primary_viewport_window();
    const auto& scene_root              = scene_builder->get_scene_root();

    m_rendertarget_node = scene_root->create_rendertarget_node(
        *m_components,
        *primary_viewport_window.get(),
        128 * 3,
        128,
        4000.0
    );

    m_rendertarget_node->set_parent(scene_root->scene().root_node);

    m_rendertarget_imgui_viewport = std::make_shared<editor::Rendertarget_imgui_viewport>(
        m_rendertarget_node.get(),
        "Hotbar Viewport",
        *m_components,
        false
    );
    m_rendertarget_imgui_viewport->set_clear_color(glm::vec4{0.0f, 0.0f, 0.0f, 0.0f});

    // Also registers rendertarget node
    imgui_windows->register_imgui_viewport(m_rendertarget_imgui_viewport);

    this->set_viewport(m_rendertarget_imgui_viewport.get());

    if (m_show)
    {
        m_rendertarget_imgui_viewport->set_enabled(true);
        m_rendertarget_node->node_data.visibility_mask |= erhe::scene::Node_visibility::visible;
        show();
    }
    else
    {
        m_rendertarget_imgui_viewport->set_enabled(false);
        m_rendertarget_node->node_data.visibility_mask &= ~erhe::scene::Node_visibility::visible;
        hide();
    }
}

void Hotbar::post_initialize()
{
    m_imgui_renderer   = get<erhe::application::Imgui_renderer>();
    m_icon_set         = get<Icon_set        >();
    m_physics_tool     = get<Physics_tool    >();
    m_operations       = get<Operations      >();
    m_selection_tool   = get<Selection_tool  >();
    m_trs_tool         = get<Trs_tool        >();
    m_viewport_windows = get<Viewport_windows>();
}

auto Hotbar::description() -> const char*
{
   return c_title.data();
}

auto Hotbar::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
#if defined(ERHE_XR_LIBRARY_OPENXR)
    {
        const auto& headset_view = get<editor::Headset_view>();
        if (headset_view)
        {
            return headset_view->get_camera();
        }
    }
#endif
    const auto viewport_window = m_viewport_windows->hover_window();
    if (!viewport_window)
    {
        return {};
    }
    return viewport_window->get_camera();
}

void Hotbar::update_once_per_frame(
    const erhe::components::Time_context& /*time_context*/
)
{
    //const auto viewport_window = m_viewport_windows->hover_window();
    //if (viewport_window)
    //{
    //    update_node_transform(viewport_window->get_camera()->world_from_node());
    //}
}

void Hotbar::update_node_transform(const glm::mat4& world_from_camera)
{
    if (!m_rendertarget_node)
    {
        return;
    }

    const glm::vec3 target_position{world_from_camera * glm::vec4{0.0, 0.0, 0.0, 1.0}};
    const glm::vec3 eye_position{world_from_camera * glm::vec4{m_x, m_y, m_z, 1.0}};
    const glm::vec3 up_direction{world_from_camera * glm::vec4{0.0, 1.0, 0.0, 0.0}};

    const glm::mat4 m = erhe::toolkit::create_look_at(
        eye_position,
        target_position,
        up_direction
    );

    m_rendertarget_node->set_world_from_node(m);
}

void Hotbar::tool_render(
    const Render_context& /*context*/
)
{
}

auto Hotbar::flags() -> ImGuiWindowFlags
{
    return
        ImGuiWindowFlags_NoBackground      |
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

auto Hotbar::get_color(const int color) -> glm::vec4&
{
    switch (color)
    {
        case 0:  return m_color_inactive;
        case 1:  return m_color_hover;
        case 2:  return m_color_active;
        default: return m_color_inactive;
    }
}

void Hotbar::imgui()
{
#if defined(ERHE_GUI_LIBRARY_IMGUI)
    //const ImVec2 button_size{64.0f, 64.0f};
    //ImGui::SliderFloat("X", &m_x, -2.0f, 2.0f);
    //ImGui::SliderFloat("Y", &m_y, -0.3f, 0.0f);
    //ImGui::SliderFloat("Z", &m_z, -2.0f, 2.0f);
    const glm::vec4 background_color{0.0f, 0.0f, 0.0f, 0.0f};
    const glm::vec4 tint_color      {1.0f, 1.0f, 1.0f, 1.0f};
    const int       frame_padding      = 0;
    const auto&     icon_rasterization = m_icon_set->get_hotbar_rasterization();
    const bool      linear{false};

    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  m_color_active);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_color_hover);
    ImGui::PushStyleColor(ImGuiCol_Button,        m_color_inactive);

    const bool is_move   = m_action == Hotbar_action::Move;
    const bool is_rotate = m_action == Hotbar_action::Rotate;
    //const bool is_push   = m_action == Hotbar_action::Push;
    //const bool is_pull   = m_action == Hotbar_action::Pull;
    const bool is_drag   = m_action == Hotbar_action::Drag;

    if (is_move) ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    const bool move   = icon_rasterization.icon_button(m_icon_set->icons.move,   frame_padding, background_color, tint_color, linear);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Move");
    }
    if (is_move) ImGui::PopStyleColor();

    if (is_rotate) ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    const bool rotate = icon_rasterization.icon_button(m_icon_set->icons.rotate, frame_padding, background_color, tint_color, linear);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Rotate");
    }
    if (is_rotate) ImGui::PopStyleColor();

    //if (is_push) ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    //const bool push   = icon_rasterization.icon_button(m_icon_set->icons.push,   frame_padding, background_color, tint_color, linear);
    //if (ImGui::IsItemHovered())
    //{
    //    ImGui::SetTooltip("Push");
    //}
    //if (is_push) ImGui::PopStyleColor();
    //
    //if (is_pull) ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    //const bool pull   = icon_rasterization.icon_button(m_icon_set->icons.pull,   frame_padding, background_color, tint_color, linear);
    //if (ImGui::IsItemHovered())
    //{
    //    ImGui::SetTooltip("Pull");
    //}
    //if (is_pull) ImGui::PopStyleColor();

    if (is_drag) ImGui::PushStyleColor(ImGuiCol_Button, m_color_active);
    const bool drag   = icon_rasterization.icon_button(m_icon_set->icons.drag,   frame_padding, background_color, tint_color, linear);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Drag");
    }
    if (is_drag) ImGui::PopStyleColor();

    if (move)
    {
        m_selection_tool->set_enable_state(true);
        m_operations->set_active_tool(m_trs_tool.get());
        m_trs_tool->set_rotate(false);
        m_trs_tool->set_translate(true);
        m_action = Hotbar_action::Move;
    }
    if (rotate)
    {
        m_selection_tool->set_enable_state(true);
        m_operations->set_active_tool(m_trs_tool.get());
        m_trs_tool->set_translate(false);
        m_trs_tool->set_rotate(true);
        m_action = Hotbar_action::Rotate;
    }
    //if (push)
    //{
    //    m_selection_tool->clear_selection();
    //    m_selection_tool->set_enable_state(false);
    //    m_operations->set_active_tool(m_physics_tool.get());
    //    m_trs_tool->set_translate(false);
    //    m_trs_tool->set_rotate(false);
    //    m_physics_tool->set_mode(Physics_tool_mode::Push);
    //    m_action = Hotbar_action::Push;
    //}
    //if (pull)
    //{
    //    m_selection_tool->clear_selection();
    //    m_selection_tool->set_enable_state(false);
    //    m_operations->set_active_tool(m_physics_tool.get());
    //    m_trs_tool->set_translate(false);
    //    m_trs_tool->set_rotate(false);
    //    m_physics_tool->set_mode(Physics_tool_mode::Pull);
    //    m_action = Hotbar_action::Pull;
    //}
    if (drag)
    {
        m_selection_tool->clear_selection();
        m_selection_tool->set_enable_state(false);
        m_operations->set_active_tool(m_physics_tool.get());
        m_trs_tool->set_translate(false);
        m_trs_tool->set_rotate(false);
        m_physics_tool->set_mode(Physics_tool_mode::Drag);
        m_action = Hotbar_action::Drag;
    }

    ImGui::PopStyleColor(3);

    const auto viewport_window = m_viewport_windows->hover_window();
    if (viewport_window)
    {
        update_node_transform(viewport_window->get_camera()->world_from_node());
    }
#endif
}

} // namespace editor
