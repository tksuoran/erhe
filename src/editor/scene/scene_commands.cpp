#include "scene/scene_commands.hpp"

#include "app_context.hpp"
#include "app_windows.hpp"
#include "items.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_host.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/scene.hpp"


namespace editor {

using erhe::Item_flags;

#pragma region Command

Create_new_camera_command::Create_new_camera_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_camera"}
    , m_context{context}
{
}

auto Create_new_camera_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_camera().operator bool();
}

Create_new_empty_node_command::Create_new_empty_node_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_empty_node"}
    , m_context{context}
{
}

auto Create_new_empty_node_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_empty_node().operator bool();
}

Create_new_light_command::Create_new_light_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_light"}
    , m_context{context}
{
}

auto Create_new_light_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_light().operator bool();
}

Create_new_rendertarget_command::Create_new_rendertarget_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_rendertarget"}
    , m_context{context}
{
}

auto Create_new_rendertarget_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_rendertarget().operator bool();
}
#pragma endregion Command

Scene_commands::Scene_commands(erhe::commands::Commands& commands, App_context& context)
    : m_context                        {context}
    , m_create_new_camera_command      {commands, context}
    , m_create_new_empty_node_command  {commands, context}
    , m_create_new_light_command       {commands, context}
    , m_create_new_rendertarget_command{commands, context}
{
    commands.register_command   (&m_create_new_camera_command);
    commands.register_command   (&m_create_new_empty_node_command);
    commands.register_command   (&m_create_new_light_command);
    commands.register_command   (&m_create_new_rendertarget_command);
    commands.bind_command_to_key(&m_create_new_camera_command,       erhe::window::Key_f2, true);
    commands.bind_command_to_key(&m_create_new_empty_node_command,   erhe::window::Key_f3, true);
    commands.bind_command_to_key(&m_create_new_light_command,        erhe::window::Key_f4, true);
    commands.bind_command_to_key(&m_create_new_rendertarget_command, erhe::window::Key_f5, true);
    commands.bind_command_to_menu(&m_create_new_camera_command,       "Create.Camera");
    commands.bind_command_to_menu(&m_create_new_empty_node_command,   "Create.Empty Node");
    commands.bind_command_to_menu(&m_create_new_light_command,        "Create.Light");
    commands.bind_command_to_menu(&m_create_new_rendertarget_command, "Create.Rendertarget");
}

auto Scene_commands::get_scene_root(erhe::scene::Node* parent) const -> Scene_root*
{
    if (parent != nullptr) {
        return static_cast<Scene_root*>(parent->get_item_host());
    }

    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    const auto  first_selected_node  = get<erhe::scene::Node>(selected_items);
    const auto  first_selected_scene = get<erhe::scene::Scene>(selected_items);
    const auto& viewport_scene_view  = m_context.scene_views->last_scene_view();

    erhe::Item_host* item_host = first_selected_node ? first_selected_node->get_item_host() : nullptr;
    if ((item_host == nullptr) && first_selected_scene) {
        item_host = first_selected_scene->get_root_node()->get_item_host();
    }
    if ((item_host == nullptr) && viewport_scene_view) {
        return viewport_scene_view->get_scene_root().get();
    }
    if (item_host == nullptr) {
        return nullptr;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    return scene_root;
}

auto Scene_commands::get_scene_root(erhe::primitive::Material* material) const -> Scene_root*
{
    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    const auto  first_selected_node  = get<erhe::scene::Node>(selected_items);
    const auto  first_selected_scene = get<erhe::scene::Scene>(selected_items);
    const auto& viewport_scene_view  = m_context.scene_views->last_scene_view();

    erhe::Item_host* item_host = (material != nullptr) ? material->get_item_host() : nullptr;
    if ((item_host == nullptr) && first_selected_node) {
        item_host = first_selected_node->get_item_host();
    }
    if ((item_host == nullptr) && first_selected_scene) {
        item_host = first_selected_scene->get_root_node()->get_item_host();
    }
    if ((item_host == nullptr) && viewport_scene_view) {
        return viewport_scene_view->get_scene_root().get();
    }
    if (item_host == nullptr) {
        return nullptr;
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    return scene_root;
}

auto Scene_commands::create_new_camera(erhe::scene::Node* parent) -> std::shared_ptr<erhe::scene::Camera>
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

auto Scene_commands::create_new_empty_node(erhe::scene::Node* parent) -> std::shared_ptr<erhe::scene::Node>
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

auto Scene_commands::create_new_light(erhe::scene::Node* parent) -> std::shared_ptr<erhe::scene::Light>
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

auto Scene_commands::create_new_rendertarget(erhe::scene::Node* parent) -> std::shared_ptr<Rendertarget_mesh>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr) {
        return {};
    }

    Selection& selection = *m_context.selection;
    const std::vector<std::shared_ptr<erhe::Item_base>>& selected_items = selection.get_selected_items();

    std::shared_ptr<erhe::scene::Camera> camera = get<erhe::scene::Camera>(selected_items);
    if (!camera) {
        return {};
    }

    // Rendertarget_mesh is Mesh (can be rendered in 3D scene) with textured rectangle vertex data, provides Texture
    auto mesh = std::make_shared<Rendertarget_mesh>(
        *m_context.graphics_device,
        *m_context.mesh_memory,
        2048,
        2048,
        600.0f
    );

    {
        // Clear once
        erhe::graphics::Render_command_encoder render_encoder = m_context.graphics_device->make_render_command_encoder(*mesh->get_render_pass());
        mesh->render_done(m_context);
    }

    mesh->layer_id = scene_root->layers().rendertarget()->id;
    mesh->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        erhe::Item_flags::translucent  |
        erhe::Item_flags::show_in_ui
    );

    // Node specifies transform for rendertarget in 3D scene
    auto node = std::make_shared<erhe::scene::Node>("rendertarget node");
    //node->set_parent_from_node(
    //    erhe::math::mat4_rotate_xz_180
    //);
    node->set_parent(scene_root->get_scene().get_root_node());

    const glm::vec3 eye_position   {0.0f, 0.0f, 0.0f};
    const glm::vec3 up_direction   {0.0f, 1.0f, 0.0f};
    const glm::vec3 target_position{0.0f, 0.0f, 1.0f};
    glm::mat4 world_from_node = erhe::math::create_look_at(eye_position, target_position, up_direction);
    node->set_world_from_node(world_from_node);
    node->attach(mesh);
    node->enable_flag_bits(
        erhe::Item_flags::rendertarget |
        erhe::Item_flags::visible      |
        erhe::Item_flags::show_in_ui
    );

    // Rendertarget_imgui_host is host for ImGui windows, uses texture from mesh
    // It is also rendergraph node
    auto rendertarget_imgui_host = std::make_shared<Rendertarget_imgui_host>(
        *m_context.imgui_renderer,
        *m_context.rendergraph,
        m_context,
        mesh.get(),
        "Rendertarget ImGui host",
        true
    );
    // Unless the shared_ptr is kept somewhere, rendertarget_imgui_host gets destroyed as soon
    // as the scope is excited.. TODO This is a temp hack, figure out who should be the owner.
    m_keep_alive.push_back(rendertarget_imgui_host);

    rendertarget_imgui_host->set_begin_callback(
        [this](erhe::imgui::Imgui_host& imgui_host) {
            m_context.app_windows->viewport_menu(imgui_host);
        }
    );

    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{
                .operations = {
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = node,
                            .parent  = (parent != nullptr)
                                ? std::static_pointer_cast<erhe::scene::Node>(parent->shared_from_this())
                                : scene_root->get_hosted_scene()->get_root_node(),
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    ),
                    std::make_shared<Node_attach_operation>(mesh, node)
                }
            }
        )
    );

    // Viewport_scene_view is a Scene_view and rendergraph node, rendering scene view to connnected consumer node
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node;
    std::shared_ptr<Viewport_scene_view> scene_view = m_context.scene_views->create_viewport_scene_view(
        *m_context.graphics_device,
        *m_context.rendergraph,
        *m_context.imgui_windows,
        *m_context.app_rendering,
        *m_context.app_settings,
        *m_context.post_processing,
        *m_context.tools,
        "rt scene view",
        scene_root->shared_from_this(),
        camera,
        4,
        rendergraph_output_node,
        false
    );

    // Viewport_window is ImGui window showing contents of scene_view rendered texture
    std::shared_ptr<Viewport_window> viewport_window = m_context.scene_views->create_viewport_window(
        *m_context.imgui_renderer,
        *m_context.imgui_windows,
        scene_view,
        rendergraph_output_node,
        "Viewport ",
        ""
    );

    // Make imgui window show in rendertarget imgui host 
    viewport_window->set_imgui_host(rendertarget_imgui_host.get());

    return mesh;
}

}
