#include "xr/null_headset_view.hpp"

namespace editor {

Headset_view::Headset_view(
    erhe::commands::Commands&       commands,
    erhe::graphics::Instance&       graphics_instance,
    erhe::rendergraph::Rendergraph& rendergraph,
    erhe::window::Context_window&   context_window,
    Editor_context&                 editor_context,
    Editor_rendering&               editor_rendering,
    Editor_settings&                editor_settings,
    Mesh_memory&                    mesh_memory,
    Scene_builder&                  scene_builder,
    Time&                           time
)
    : Scene_view{editor_context, Viewport_config{}}
{
    static_cast<void>(commands);
    static_cast<void>(graphics_instance);
    static_cast<void>(rendergraph);
    static_cast<void>(context_window);
    static_cast<void>(editor_rendering);
    static_cast<void>(editor_settings);
    static_cast<void>(mesh_memory);
    static_cast<void>(scene_builder);
    static_cast<void>(time);
}

void Headset_view::render(const Render_context&)
{
}

void Headset_view::render_headset()
{
}

auto Headset_view::get_scene_root() const -> std::shared_ptr<Scene_root>
{
    return {};
}

auto Headset_view::get_root_node() const -> std::shared_ptr<erhe::scene::Node>
{
    return {};
}

auto Headset_view::get_camera() const -> std::shared_ptr<erhe::scene::Camera>
{
    return {};
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

} // namespace editor

