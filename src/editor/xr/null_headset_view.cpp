#include "xr/null_headset_view.hpp"

namespace editor {

Headset_view::Headset_view(
    erhe::commands::Commands&       commands,
    erhe::graphics::Device&         graphics_device,
    erhe::imgui::Imgui_renderer&    imgui_renderer,
    erhe::imgui::Imgui_windows&     imgui_windows,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window&   context_window,
#if defined(ERHE_XR_LIBRARY_OPENXR)
    erhe::xr::Headset*              headset,
#endif
    App_context&                    app_context,
    App_rendering&                  app_rendering,
    App_settings&                   app_settings
)
    : Scene_view{app_context, Viewport_config{}}
    , erhe::imgui::Imgui_window{imgui_renderer, imgui_windows, "Headset", "headset"}
{
    static_cast<void>(commands);
    static_cast<void>(graphics_device);
    static_cast<void>(rendergraph);
    static_cast<void>(context_window);
    static_cast<void>(app_rendering);
    static_cast<void>(app_settings);
}

void Headset_view::render(const Render_context&)
{
}

void Headset_view::update_fixed_step()
{
}

void Headset_view::render_headset()
{
}

auto Headset_view::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return {};
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return {};
}

auto Headset_view::get_perspective_scale () const -> float
{
    return 1.0f;
}

auto Headset_view::get_rendergraph_node() -> erhe::rendergraph::Rendergraph_node*
{
    return {};
}

auto Headset_view::get_shadow_render_node() const -> Shadow_render_node*
{
    return {};
}

//void Headset_view::add_finger_input(
//    const Finger_point& finger_input
//)
//{
//    m_finger_inputs.push_back(finger_input);
//}

[[nodiscard]] auto Headset_view::finger_to_viewport_distance_threshold() const -> float
{
    return 0.0f;
}

[[nodiscard]] auto Headset_view::get_headset() const -> void*
{
    return nullptr;
}

void Headset_view::begin_frame()
{
}

void Headset_view::end_frame()
{
}

//// TODO
//// 
//// void Headset_view::imgui()
//// {
////     m_mouse_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
//// 
////     ImGui::Checkbox("Head Tracking Enabled", &m_head_tracking_enabled);
//// }

}

