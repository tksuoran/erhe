#include "editor_view_client.hpp"

#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/tools.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/application/configuration.hpp"
#include "erhe/application/imgui/imgui_renderer.hpp"
#include "erhe/application/imgui/imgui_windows.hpp"
#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/time.hpp"
#include "erhe/application/view.hpp"
#include "erhe/graphics/buffer_transfer_queue.hpp"
#include "erhe/graphics/debug.hpp"
#include "erhe/physics/iworld.hpp"
#include "erhe/scene/scene.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

Editor_view_client::Editor_view_client()
    : Component{c_type_name}
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
    ERHE_PROFILE_FUNCTION

    get<erhe::application::View>()->set_client(this);
}

void Editor_view_client::post_initialize()
{
    m_commands         = get<erhe::application::Commands      >();
    m_configuration    = get<erhe::application::Configuration >();
    m_imgui_windows    = get<erhe::application::Imgui_windows >();
    m_imgui_renderer   = get<erhe::application::Imgui_renderer>();
    m_render_graph     = get<erhe::application::Rendergraph   >();
    m_editor_rendering = get<Editor_rendering>();
    m_tools            = get<Tools           >();
    m_viewport_windows = get<Viewport_windows>();
}

// TODO Something nicer
void Editor_view_client::update_fixed_step(const erhe::components::Time_context& time_context)
{
    const auto& scene_builder   = get<Scene_builder>();
    const auto& test_scene_root = scene_builder->get_scene_root();

    if (m_configuration->physics.static_enable)
    {
        test_scene_root->physics_world().update_fixed_step(time_context.dt);
    }
}

void Editor_view_client::update()
{
    {
        // TODO something nicer
        const auto& scene_builder = get<Scene_builder>();
        scene_builder->buffer_transfer_queue().flush();
        // animate_lights(time_context.time);
    }

    get<erhe::application::Time>()->update_once_per_frame();

    m_editor_rendering->begin_frame  ();
    m_imgui_windows   ->imgui_windows();
    m_render_graph    ->execute      ();
    m_imgui_renderer  ->next_frame   ();
    m_editor_rendering->end_frame    ();
    m_commands        ->on_update();
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

