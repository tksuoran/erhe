#include "scene/scene_commands.hpp"

#include "editor_context.hpp"
#include "editor_windows.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_viewport.hpp"
#include "scene/node_raytrace.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_window.hpp"
#include "scene/viewport_windows.hpp"
#include "tools/selection_tool.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_imgui/imgui_windows.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"

namespace editor
{

using erhe::Item_flags;

#pragma region Command

Create_new_camera_command::Create_new_camera_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "scene.create_new_camera"}
    , m_context{context}
{
}

auto Create_new_camera_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_camera().operator bool();
}

Create_new_empty_node_command::Create_new_empty_node_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "scene.create_new_empty_node"}
    , m_context{context}
{
}

auto Create_new_empty_node_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_empty_node().operator bool();
}

Create_new_light_command::Create_new_light_command(
    erhe::commands::Commands& commands,
    Editor_context&           context
)
    : Command  {commands, "scene.create_new_light"}
    , m_context{context}
{
}

auto Create_new_light_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_light().operator bool();
}
#pragma endregion Command

Scene_commands::Scene_commands(
    erhe::commands::Commands& commands,
    Editor_context&           editor_context
)
    : m_context                      {editor_context}
    , m_create_new_camera_command    {commands, editor_context}
    , m_create_new_empty_node_command{commands, editor_context}
    , m_create_new_light_command     {commands, editor_context}

{
    commands.register_command   (&m_create_new_camera_command);
    commands.register_command   (&m_create_new_empty_node_command);
    commands.register_command   (&m_create_new_light_command);
    commands.bind_command_to_key(&m_create_new_camera_command,     erhe::window::Key_f2, true);
    commands.bind_command_to_key(&m_create_new_empty_node_command, erhe::window::Key_f3, true);
    commands.bind_command_to_key(&m_create_new_light_command,      erhe::window::Key_f4, true);
}

auto Scene_commands::get_scene_root(
    erhe::scene::Node* parent
) const -> Scene_root*
{
    if (parent != nullptr) {
        return static_cast<Scene_root*>(parent->get_item_host());
    }

    const auto  first_selected_node  = m_context.selection->get<erhe::scene::Node>();
    const auto  first_selected_scene = m_context.selection->get<erhe::scene::Scene>();
    const auto& viewport_window      = m_context.viewport_windows->last_window();

    erhe::Item_host* item_host = first_selected_node ? first_selected_node->get_item_host() : nullptr;
    if ((item_host == nullptr) && first_selected_scene) {
        item_host = first_selected_scene->get_root_node()->get_item_host();
    }
    if ((item_host == nullptr) && viewport_window) {
        return viewport_window->get_scene_root().get();
    }
    if (item_host == nullptr) {
        return nullptr;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    return scene_root;
}

auto Scene_commands::get_scene_root(
    erhe::primitive::Material* material
) const -> Scene_root*
{
    const auto  first_selected_node  = m_context.selection->get<erhe::scene::Node>();
    const auto  first_selected_scene = m_context.selection->get<erhe::scene::Scene>();
    const auto& viewport_window      = m_context.viewport_windows->last_window();

    erhe::Item_host* item_host = (material != nullptr) ? material->get_item_host() : nullptr;
    if ((item_host == nullptr) && first_selected_node) {
        item_host = first_selected_node->get_item_host();
    }
    if ((item_host == nullptr) && first_selected_scene) {
        item_host = first_selected_scene->get_root_node()->get_item_host();
    }
    if ((item_host == nullptr) && viewport_window) {
        return viewport_window->get_scene_root().get();
    }
    if (item_host == nullptr) {
        return nullptr;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    return scene_root;
}

auto Scene_commands::create_new_camera(
    erhe::scene::Node* parent
) -> std::shared_ptr<erhe::scene::Camera>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr) {
        return {};
    }

    auto new_node   = std::make_shared<erhe::scene::Node>("new camera node");
    auto new_camera = std::make_shared<erhe::scene::Camera>("new camera");
    new_node  ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    new_camera->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{
                .operations = {
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = new_node,
                            .parent  = (parent != nullptr)
                                ? std::static_pointer_cast<erhe::scene::Node>(parent->shared_from_this())
                                : scene_root->get_hosted_scene()->get_root_node(),
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    ),
                    std::make_shared<Node_attach_operation>(new_camera, new_node)
                }
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
    if (scene_root == nullptr) {
        return {};
    }

    auto new_empty_node = std::make_shared<erhe::scene::Node>("new empty node");
    new_empty_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_context.operation_stack->queue(
        std::make_shared<Item_insert_remove_operation>(
            Item_insert_remove_operation::Parameters{
                .context = m_context,
                .item    = new_empty_node,
                .parent  = (parent != nullptr)
                    ? std::static_pointer_cast<erhe::scene::Node>(parent->shared_from_this())
                    : scene_root->get_hosted_scene()->get_root_node(),
                .mode    = Item_insert_remove_operation::Mode::insert
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
    if (scene_root == nullptr) {
        return {};
    }

    auto new_node  = std::make_shared<erhe::scene::Node>("new light node");
    auto new_light = std::make_shared<erhe::scene::Light>("new light");
    new_node ->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    new_light->enable_flag_bits(erhe::Item_flags::content | Item_flags::show_in_ui);
    new_light->layer_id = scene_root->layers().light()->id;
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{
                .operations = {
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = new_node,
                            .parent  = (parent != nullptr)
                                ? std::static_pointer_cast<erhe::scene::Node>(parent->shared_from_this())
                                : scene_root->get_hosted_scene()->get_root_node(),
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    ),
                    std::make_shared<Node_attach_operation>(new_light, new_node)
                }
            }
        )
    );

    return new_light;
}

auto Scene_commands::create_new_rendertarget(
    erhe::scene::Node* parent
) -> std::shared_ptr<Rendertarget_mesh>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr) {
        return {};
    }

    auto new_node = std::make_shared<erhe::scene::Node>("new light node");
    auto new_mesh = std::make_shared<Rendertarget_mesh>(
        *m_context.graphics_instance,
        *m_context.mesh_memory,
        2048,
        2048,
        600.0f
    );
    new_mesh->layer_id = scene_root->layers().rendertarget()->id;
    new_mesh->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        erhe::Item_flags::translucent  |
        erhe::Item_flags::show_in_ui
    );

    new_node = std::make_shared<erhe::scene::Node>("Hud RT node");
    new_node->set_parent_from_node(
        erhe::math::mat4_rotate_xz_180
    );
    new_node->set_parent(scene_root->get_scene().get_root_node());
    new_node->attach(new_mesh);
    new_node->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        erhe::Item_flags::show_in_ui
    );

    auto rendertarget_imgui_viewport = std::make_shared<Rendertarget_imgui_viewport>(
        *m_context.imgui_renderer,
        *m_context.rendergraph,
        m_context,
        new_mesh.get(),
        "Rendertarget Viewport",
        true
    );

    rendertarget_imgui_viewport->set_begin_callback(
        [this](erhe::imgui::Imgui_viewport& imgui_viewport) {
            m_context.editor_windows->viewport_menu(imgui_viewport);
        }
    );

    new_mesh->layer_id = scene_root->layers().rendertarget()->id;
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{
                .operations = {
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = new_node,
                            .parent  = (parent != nullptr)
                                ? std::static_pointer_cast<erhe::scene::Node>(parent->shared_from_this())
                                : scene_root->get_hosted_scene()->get_root_node(),
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    ),
                    std::make_shared<Node_attach_operation>(new_mesh, new_node)
                }
            }
        )
    );

    return new_mesh;
}

} // namespace editor
