#include "scene/scene_commands.hpp"
#include "editor_scenes.hpp"
#include "operations/operation_stack.hpp"
#include "operations/insert_operation.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"

#include "erhe/application/commands/commands.hpp"
#include "erhe/scene/camera.hpp"
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
    m_editor_scenes   = get<Editor_scenes >();
    m_operation_stack = get<Operation_stack>();
}

auto Scene_commands::get_scene_root(erhe::scene::Node* parent) const -> Scene_root*
{
    if (parent != nullptr)
    {
        return reinterpret_cast<Scene_root*>(parent->get_scene_host());
    }

    const auto& selection_tool  = get<Selection_tool>();
    const auto  selection_front = selection_tool->selection().empty()
        ? std::shared_ptr<erhe::scene::Node>()
        : selection_tool->selection().front();

    const auto& viewport_window = get<Viewport_windows>()->last_window();

    erhe::scene::Scene_host* scene_host = selection_front
        ? reinterpret_cast<Scene_root*>(selection_front->node_data.host)
        : viewport_window
            ? viewport_window->get_scene_root().get()
            : nullptr;
    if (scene_host == nullptr)
    {
        return nullptr;
    }

    Scene_root* scene_root = reinterpret_cast<Scene_root*>(scene_host);
    return scene_root;
}

auto Scene_commands::create_new_camera(
    erhe::scene::Node* parent
) -> std::shared_ptr<erhe::scene::Camera>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr)
    {
        return {};
    }

    auto new_camera = std::make_shared<erhe::scene::Camera>("new camera");
    new_camera->node_data.visibility_mask = erhe::scene::Node_visibility::content;
    get<Operation_stack>()->push(
        std::make_shared<Node_insert_remove_operation>(
            Node_insert_remove_operation::Parameters{
                .selection_tool = get<Selection_tool>().get(),
                .node           = new_camera,
                .parent         = (parent != nullptr)
                    ? parent->shared_from_this()
                    : scene_root->get_scene()->root_node,
                .mode           = Scene_item_operation::Mode::insert
            }
        )
    );

    return new_camera;
}

auto Scene_commands::create_new_empty_node(
    erhe::scene::Node* parent
) -> std::shared_ptr<erhe::scene::Node>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr)
    {
        return {};
    }

    auto new_empty_node = std::make_shared<erhe::scene::Node>("new empty node");
    new_empty_node->node_data.visibility_mask = erhe::scene::Node_visibility::content;
    get<Operation_stack>()->push(
        std::make_shared<Node_insert_remove_operation>(
            Node_insert_remove_operation::Parameters{
                .selection_tool = get<Selection_tool>().get(),
                .node           = new_empty_node,
                .parent         = (parent != nullptr)
                    ? parent->shared_from_this()
                    : scene_root->get_scene()->root_node,
                .mode           = Scene_item_operation::Mode::insert
            }
        )
    );

    return new_empty_node;
}

auto Scene_commands::create_new_light(
    erhe::scene::Node* parent
) -> std::shared_ptr<erhe::scene::Light>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr)
    {
        return {};
    }

    auto new_light = std::make_shared<erhe::scene::Light>("new light");
    new_light->node_data.visibility_mask = erhe::scene::Node_visibility::content;
    new_light->layer_id = scene_root->layers().light()->id.get_id();
    get<Operation_stack>()->push(
        std::make_shared<Node_insert_remove_operation>(
            Node_insert_remove_operation::Parameters{
                .selection_tool = get<Selection_tool>().get(),
                .node           = new_light,
                .parent         = (parent != nullptr)
                    ? parent->shared_from_this()
                    : scene_root->get_scene()->root_node,
                .mode           = Scene_item_operation::Mode::insert
            }
        )
    );

    return new_light;
}

} // namespace editor
