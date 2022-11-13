#include "scene/scene_commands.hpp"
#include "scene/scene_root.hpp"

#include "editor_scenes.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/toolkit/profile.hpp"

namespace editor
{

auto Create_new_camera_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_commands.create_new_camera().operator bool();
}

auto Create_new_empty_node_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_commands.create_new_empty_node().operator bool();
}

auto Create_new_light_command::try_call(
    erhe::application::Command_context& context
) -> bool
{
    static_cast<void>(context);

    return m_scene_commands.create_new_light().operator bool();
}

Scene_commands::Scene_commands()
    : erhe::components::Component    {c_type_name}
    , m_create_new_camera_command    {*this}
    , m_create_new_empty_node_command{*this}
    , m_create_new_light_command     {*this}
{
}

Scene_commands::~Scene_commands() noexcept
{
}

void Scene_commands::declare_required_components()
{
    require<erhe::application::Commands>();
}

void Scene_commands::initialize_component()
{
    ERHE_PROFILE_FUNCTION

    const auto commands = get<erhe::application::Commands>();

    commands->register_command   (&m_create_new_camera_command);
    commands->register_command   (&m_create_new_empty_node_command);
    commands->register_command   (&m_create_new_light_command);
    commands->bind_command_to_key(&m_create_new_camera_command,     erhe::toolkit::Key_f2, true);
    commands->bind_command_to_key(&m_create_new_empty_node_command, erhe::toolkit::Key_f3, true);
    commands->bind_command_to_key(&m_create_new_light_command,      erhe::toolkit::Key_f4, true);
}

void Scene_commands::post_initialize()
{
    m_editor_scenes = get<Editor_scenes>();
}

auto Scene_commands::create_new_camera() -> std::shared_ptr<erhe::scene::Camera>
{
    const auto& scene_root = m_editor_scenes->get_current_scene_root();
    if (!scene_root)
    {
        return {};
    }

    return scene_root->create_new_camera();
}

auto Scene_commands::create_new_empty_node() -> std::shared_ptr<erhe::scene::Node>
{
    const auto& scene_root = m_editor_scenes->get_current_scene_root();
    if (!scene_root)
    {
        return {};
    }

    return scene_root->create_new_empty_node();
}

auto Scene_commands::create_new_light() -> std::shared_ptr<erhe::scene::Light>
{
    const auto& scene_root = m_editor_scenes->get_current_scene_root();
    if (!scene_root)
    {
        return {};
    }

    return scene_root->create_new_light();
}

} // namespace editor
