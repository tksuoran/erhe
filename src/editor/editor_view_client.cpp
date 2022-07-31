#include "editor_view_client.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "windows/viewport_windows.hpp"

#include "erhe/application/configuration.hpp"
#include "erhe/application/renderers/imgui_renderer.hpp"
#include "erhe/application/imgui_windows.hpp"
#include "erhe/application/render_graph.hpp"
#include "erhe/application/view.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/scene.hpp"

namespace editor
{

Editor_view_client::Editor_view_client()
    : Component{c_label}
{
}

Editor_view_client::~Editor_view_client() noexcept
{
}

void Editor_view_client::declare_required_components()
{
    require<erhe::application::View>();
}

void Editor_view_client::initialize_component()
{
    get<erhe::application::View>()->set_client(this);
}

void Editor_view_client::post_initialize()
{
    m_imgui_windows    = get<erhe::application::Imgui_windows >();
    m_imgui_renderer   = get<erhe::application::Imgui_renderer>();
    m_render_graph     = get<erhe::application::Render_graph  >();
    m_editor_rendering = get<Editor_rendering>();
    m_viewport_windows = get<Viewport_windows>();
}

// TODO Something nicer
void Editor_view_client::update_fixed_step(const erhe::components::Time_context& time_context)
{
    const auto& scene_builder   = get<Scene_builder>();
    const auto& test_scene_root = scene_builder->get_scene_root();

    test_scene_root->physics_world().update_fixed_step(time_context.dt);
}

void Editor_view_client::update()
{
    {
        // TODO something nicer
        const auto& scene_builder = get<Scene_builder>();
        scene_builder->buffer_transfer_queue().flush();
        // animate_lights(time_context.time);
    }
    m_editor_rendering->begin_frame();

    if (erhe::components::Component::get<erhe::application::Configuration>()->imgui.enabled)
    {
        m_imgui_windows->imgui_windows();
    }

    m_render_graph  ->execute();
    m_imgui_renderer->next_frame();

    m_editor_rendering->end_frame();
}

void Editor_view_client::update_keyboard(
    const bool                   pressed,
    const erhe::toolkit::Keycode code,
    const uint32_t               modifier_mask
)
{
    m_viewport_windows->update_keyboard(pressed, code, modifier_mask);
}

void Editor_view_client::update_mouse(
    const erhe::toolkit::Mouse_button button,
    const int                         count
)
{
    m_viewport_windows->update_mouse(button, count);
}

void Editor_view_client::update_mouse(const double x, const double y)
{
    m_viewport_windows->update_mouse(x, y);
}

} // namespace hextiles
