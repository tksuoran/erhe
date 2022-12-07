#include "editor_view_client.hpp"

#include "editor_message_bus.hpp"
#include "editor_rendering.hpp"
#include "editor_scenes.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_message_bus.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/hotbar.hpp"
#include "tools/hud.hpp"
#include "tools/tools.hpp"
#include "tools/trs_tool.hpp"

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
    m_editor_message_bus = get<Editor_message_bus>();
    m_editor_rendering   = get<Editor_rendering  >();
    m_hotbar             = get<Hotbar            >();
    m_hud                = get<Hud               >();
    m_scene_message_bus  = get<Scene_message_bus >();
    m_tools              = get<Tools             >();
    m_trs_tool           = get<Trs_tool          >();
    m_viewport_windows   = get<Viewport_windows  >();
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
    m_editor_message_bus->update();
    m_scene_message_bus->update();

    const auto& last_window = m_viewport_windows->last_window();
    if (last_window)
    {
        const auto& camera = last_window->get_camera();
        if (camera)
        {
            const auto* camera_node = camera->get_node();
            if (camera_node != nullptr)
            {
                // TODO check this logic, probably not all calls to update_node_transforms() are needed
                last_window->get_scene_root()->get_scene()->update_node_transforms();
                const auto config = get<erhe::application::Configuration>()->headset;
                if (!config.openxr)
                {
                    const auto& world_from_camera = camera_node->world_from_node();

                    m_hotbar->update_node_transform(world_from_camera);
                    m_hud   ->update_node_transform(world_from_camera);
                }
                last_window->get_scene_root()->get_scene()->update_node_transforms();
                m_trs_tool->update_for_view(last_window.get());
                m_tools->get_tool_scene_root()->get_scene()->update_node_transforms();
            }
        }
    }

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

