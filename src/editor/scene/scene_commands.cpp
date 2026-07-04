#include "scene/scene_commands.hpp"

#include "config/generated/make_mesh_args.hpp"
#include "config/generated/graphics_preset_entry.hpp"

#include "app_context.hpp"
#include "app_message_bus.hpp"
#include "app_scenes.hpp"
#include "app_settings.hpp"
#include "app_windows.hpp"
#include "content_library/content_library.hpp"
#include "editor_log.hpp"
#include "windows/item_tree_window.hpp"
#include "items.hpp"
#include "operations/compound_operation.hpp"
#include "operations/item_insert_remove_operation.hpp"
#include "operations/node_attach_operation.hpp"
#include "operations/operation_stack.hpp"
#include "mesh_rendertarget_view.hpp"
#include "rendertarget_mesh.hpp"
#include "rendertarget_imgui_host.hpp"
#include "scene/collision_shape_from_mesh.hpp"
#include "scene/node_joint.hpp"
#include "scene/node_physics.hpp"
#include "scene/scene_builder.hpp"
#include "scene/scene_root.hpp"
#include "scene/viewport_scene_view.hpp"
#include "scene/viewport_scene_views.hpp"
#include "tools/selection_tool.hpp"
#include "windows/viewport_window.hpp"

#include "erhe_commands/commands.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_physics/icollision_shape.hpp"
#include "erhe_physics/irigid_body.hpp"
#include "erhe_physics/physics_joint_settings.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_scene/camera.hpp"
#include "erhe_scene/layout.hpp"
#include "erhe_scene/projection.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include <fmt/format.h>


namespace editor {

using erhe::Item_flags;

#pragma region Command

Create_new_scene_command::Create_new_scene_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_scene"}
    , m_context{context}
{
}

auto Create_new_scene_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_scene().operator bool();
}

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

Create_new_layout_command::Create_new_layout_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_layout"}
    , m_context{context}
{
}

auto Create_new_layout_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_layout().operator bool();
}

Create_new_rigid_body_command::Create_new_rigid_body_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_rigid_body"}
    , m_context{context}
{
}

auto Create_new_rigid_body_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_rigid_body().operator bool();
}

Create_new_joint_command::Create_new_joint_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.create_new_joint"}
    , m_context{context}
{
}

auto Create_new_joint_command::try_call() -> bool
{
    return m_context.scene_commands->create_new_joint().operator bool();
}

Add_cameras_command::Add_cameras_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_cameras"}
    , m_context{context}
{
}

auto Add_cameras_command::try_call() -> bool
{
    m_context.scene_builder->add_cameras(m_args);
    return true;
}

void Add_cameras_command::apply_args(const Add_cameras_args& args)
{
    m_args = args;
}

Add_room_command::Add_room_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_room"}
    , m_context{context}
{
}

auto Add_room_command::try_call() -> bool
{
    m_context.scene_builder->add_room(m_args);
    return true;
}

void Add_room_command::apply_args(const Add_room_args& args)
{
    m_args = args;
}

Add_lights_command::Add_lights_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_lights"}
    , m_context{context}
{
}

auto Add_lights_command::try_call() -> bool
{
    m_context.scene_builder->add_lights(m_args);
    return true;
}

void Add_lights_command::apply_args(const Add_lights_args& args)
{
    // The shadow-light clamp lives in Scene_builder::add_lights so
    // callers that bypass apply_args (key bindings, MCP, UI buttons)
    // see the same clamp. apply_args used to clamp here too; that
    // duplicate path was removed to keep a single source of truth.
    m_args = args;
}

Add_platonic_solids_command::Add_platonic_solids_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_platonic_solids"}
    , m_context{context}
{
}

auto Add_platonic_solids_command::try_call() -> bool
{
    m_context.scene_builder->add_platonic_solids(m_make_mesh_config);
    return true;
}

void Add_platonic_solids_command::set_make_mesh_config(const Make_mesh_config& config)
{
    m_make_mesh_config = config;
}

void Add_platonic_solids_command::apply_args(const Make_mesh_args& args)
{
    m_make_mesh_config = Make_mesh_config{};
    m_make_mesh_config.instance_count = args.instance_count;
    m_make_mesh_config.instance_gap   = args.instance_gap;
    m_make_mesh_config.object_scale   = args.object_scale;
    m_make_mesh_config.detail         = args.detail;
    m_make_mesh_config.mass_scale     = args.mass_scale;
}

Add_johnson_solids_command::Add_johnson_solids_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_johnson_solids"}
    , m_context{context}
{
}

auto Add_johnson_solids_command::try_call() -> bool
{
    m_context.scene_builder->add_johnson_solids(m_make_mesh_config);
    return true;
}

void Add_johnson_solids_command::set_make_mesh_config(const Make_mesh_config& config)
{
    m_make_mesh_config = config;
}

void Add_johnson_solids_command::apply_args(const Make_mesh_args& args)
{
    m_make_mesh_config = Make_mesh_config{};
    m_make_mesh_config.instance_count = args.instance_count;
    m_make_mesh_config.instance_gap   = args.instance_gap;
    m_make_mesh_config.object_scale   = args.object_scale;
    m_make_mesh_config.detail         = args.detail;
    m_make_mesh_config.mass_scale     = args.mass_scale;
}

Add_curved_shapes_command::Add_curved_shapes_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_curved_shapes"}
    , m_context{context}
{
}

auto Add_curved_shapes_command::try_call() -> bool
{
    m_context.scene_builder->add_curved_shapes(m_make_mesh_config);
    return true;
}

void Add_curved_shapes_command::set_make_mesh_config(const Make_mesh_config& config)
{
    m_make_mesh_config = config;
}

void Add_curved_shapes_command::apply_args(const Make_mesh_args& args)
{
    m_make_mesh_config = Make_mesh_config{};
    m_make_mesh_config.instance_count = args.instance_count;
    m_make_mesh_config.instance_gap   = args.instance_gap;
    m_make_mesh_config.object_scale   = args.object_scale;
    m_make_mesh_config.detail         = args.detail;
    m_make_mesh_config.mass_scale     = args.mass_scale;
}

Add_chain_command::Add_chain_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_chain"}
    , m_context{context}
{
}

auto Add_chain_command::try_call() -> bool
{
    m_context.scene_builder->add_torus_chain(m_make_mesh_config, true);
    return true;
}

void Add_chain_command::set_make_mesh_config(const Make_mesh_config& config)
{
    m_make_mesh_config = config;
}

void Add_chain_command::apply_args(const Make_mesh_args& args)
{
    m_make_mesh_config = Make_mesh_config{};
    m_make_mesh_config.instance_count = args.instance_count;
    m_make_mesh_config.instance_gap   = args.instance_gap;
    m_make_mesh_config.object_scale   = args.object_scale;
    m_make_mesh_config.detail         = args.detail;
    m_make_mesh_config.mass_scale     = args.mass_scale;
}

Add_toruses_command::Add_toruses_command(erhe::commands::Commands& commands, App_context& context)
    : Command  {commands, "scene.add_toruses"}
    , m_context{context}
{
}

auto Add_toruses_command::try_call() -> bool
{
    m_context.scene_builder->add_torus_chain(m_make_mesh_config, false);
    return true;
}

void Add_toruses_command::set_make_mesh_config(const Make_mesh_config& config)
{
    m_make_mesh_config = config;
}

void Add_toruses_command::apply_args(const Make_mesh_args& args)
{
    m_make_mesh_config = Make_mesh_config{};
    m_make_mesh_config.instance_count = args.instance_count;
    m_make_mesh_config.instance_gap   = args.instance_gap;
    m_make_mesh_config.object_scale   = args.object_scale;
    m_make_mesh_config.detail         = args.detail;
    m_make_mesh_config.mass_scale     = args.mass_scale;
}
#pragma endregion Command

Scene_commands::Scene_commands(erhe::commands::Commands& commands, App_context& context)
    : m_context                        {context}
    , m_create_new_scene_command       {commands, context}
    , m_create_new_camera_command      {commands, context}
    , m_create_new_empty_node_command  {commands, context}
    , m_create_new_light_command       {commands, context}
    , m_create_new_layout_command      {commands, context}
    , m_create_new_rendertarget_command{commands, context}
    , m_create_new_rigid_body_command  {commands, context}
    , m_create_new_joint_command       {commands, context}
    , m_add_cameras_command            {commands, context}
    , m_add_room_command               {commands, context}
    , m_add_lights_command             {commands, context}
    , m_add_platonic_solids_command    {commands, context}
    , m_add_johnson_solids_command     {commands, context}
    , m_add_curved_shapes_command      {commands, context}
    , m_add_chain_command              {commands, context}
    , m_add_toruses_command            {commands, context}
{
    commands.register_command   (&m_create_new_scene_command);
    commands.register_command   (&m_create_new_camera_command);
    commands.register_command   (&m_create_new_empty_node_command);
    commands.register_command   (&m_create_new_light_command);
    commands.register_command   (&m_create_new_layout_command);
    commands.register_command   (&m_create_new_rendertarget_command);
    commands.register_command   (&m_create_new_rigid_body_command);
    commands.register_command   (&m_create_new_joint_command);
    commands.register_command   (&m_add_cameras_command);
    commands.register_command   (&m_add_room_command);
    commands.register_command   (&m_add_lights_command);
    commands.register_command   (&m_add_platonic_solids_command);
    commands.register_command   (&m_add_johnson_solids_command);
    commands.register_command   (&m_add_curved_shapes_command);
    commands.register_command   (&m_add_chain_command);
    commands.register_command   (&m_add_toruses_command);
    commands.bind_command_to_key(&m_create_new_camera_command,       erhe::window::Key_f2, true);
    commands.bind_command_to_key(&m_create_new_empty_node_command,   erhe::window::Key_f3, true);
    commands.bind_command_to_key(&m_create_new_light_command,        erhe::window::Key_f4, true);
    commands.bind_command_to_key(&m_create_new_rendertarget_command, erhe::window::Key_f5, true);
    commands.bind_command_to_key(&m_create_new_layout_command,       erhe::window::Key_f6, true);
    commands.bind_command_to_menu(&m_create_new_scene_command,        "Create.Scene");
    commands.bind_command_to_menu(&m_create_new_camera_command,       "Create.Camera");
    commands.bind_command_to_menu(&m_create_new_empty_node_command,   "Create.Empty Node");
    commands.bind_command_to_menu(&m_create_new_light_command,        "Create.Light");
    commands.bind_command_to_menu(&m_create_new_layout_command,       "Create.Layout");
    commands.bind_command_to_menu(&m_create_new_rendertarget_command, "Create.Rendertarget");
    commands.bind_command_to_menu(&m_create_new_rigid_body_command,   "Create.Rigid Body");
    commands.bind_command_to_menu(&m_create_new_joint_command,        "Create.Joint");
}

auto Scene_commands::get_add_cameras_command() -> Add_cameras_command&
{
    return m_add_cameras_command;
}

auto Scene_commands::get_add_room_command() -> Add_room_command&
{
    return m_add_room_command;
}

auto Scene_commands::get_add_lights_command() -> Add_lights_command&
{
    return m_add_lights_command;
}

auto Scene_commands::get_add_platonic_solids_command() -> Add_platonic_solids_command&
{
    return m_add_platonic_solids_command;
}

auto Scene_commands::get_add_johnson_solids_command() -> Add_johnson_solids_command&
{
    return m_add_johnson_solids_command;
}

auto Scene_commands::get_add_curved_shapes_command() -> Add_curved_shapes_command&
{
    return m_add_curved_shapes_command;
}

auto Scene_commands::get_add_chain_command() -> Add_chain_command&
{
    return m_add_chain_command;
}

auto Scene_commands::get_add_toruses_command() -> Add_toruses_command&
{
    return m_add_toruses_command;
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
        // Nothing selected and no viewport hovered yet: when exactly one
        // scene exists, use it as the target.
        return m_context.app_scenes->get_single_scene_root().get();
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
        // Nothing selected and no viewport hovered yet: when exactly one
        // scene exists, use it as the target.
        return m_context.app_scenes->get_single_scene_root().get();
    }
    Scene_root* scene_root = static_cast<Scene_root*>(item_host);
    return scene_root;
}

auto Scene_commands::create_new_scene() -> std::shared_ptr<Scene_root>
{
    // Name new scenes uniquely so they stay distinguishable in the Hierarchy
    // window and to the name-based MCP tools.
    const std::string scene_name = fmt::format("Scene {}", m_context.app_scenes->get_scene_roots().size() + 1);

    // Like a loaded scene (and unlike the scene.create default scene, whose
    // library is populated by the scene.add_* startup commands), the new scene
    // gets its own, empty content library.
    std::shared_ptr<Content_library> content_library = std::make_shared<Content_library>();
    const bool enable_physics = m_context.editor_settings->physics.static_enable;
    std::shared_ptr<Scene_root> scene_root = std::make_shared<Scene_root>(
        m_context.app_message_bus,
        content_library,
        scene_name,
        enable_physics
    );

    // The only scene content: a default camera matching the default scene's
    // "Camera A" (Scene_builder::make_camera with the Add_cameras_args
    // defaults). Added before the scene is registered / has a viewport, so no
    // other part can observe the scene camera-less; not routed through the
    // Operation_stack because the scene creation itself is not undoable.
    std::shared_ptr<erhe::scene::Node>   camera_node = std::make_shared<erhe::scene::Node>("Camera");
    std::shared_ptr<erhe::scene::Camera> camera      = std::make_shared<erhe::scene::Camera>("Camera");
    camera->projection()->fov_y           = glm::radians(35.0f);
    camera->projection()->projection_type = erhe::scene::Projection::Type::perspective_vertical;
    camera->projection()->z_near          = 0.03f;
    camera->projection()->z_far           = 64.0f;
    camera->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui | Item_flags::show_debug_visualizations);
    camera->set_exposure(1.0f);
    camera->set_shadow_range(22.0f);
    camera_node->attach(camera);
    camera_node->set_parent_from_node(
        erhe::math::create_look_at(
            glm::vec3{0.0f, 1.6f,  3.0f}, // eye
            glm::vec3{0.0f, 0.25f, 0.0f}, // center
            glm::vec3{0.0f, 1.0f,  0.0f}  // up
        )
    );
    camera_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    camera_node->set_parent(scene_root->get_hosted_scene()->get_root_node());

    scene_root->register_to_editor_scenes(*m_context.app_scenes);

    // The content library browser is shown nested under the Scene row in the
    // Hierarchy window (#240).
    std::shared_ptr<Item_tree_window> browser_window = scene_root->make_browser_window(
        *m_context.imgui_renderer,
        *m_context.imgui_windows,
        m_context,
        *m_context.app_settings
    );
    browser_window->show_window();

    // A viewport window looking through the new camera. create_viewport_scene_view
    // is called directly (rather than open_new_viewport_scene_view) so the new
    // scene's own camera is used even when a camera of another scene is selected.
    std::shared_ptr<erhe::rendergraph::Rendergraph_node> rendergraph_output_node;
    std::shared_ptr<Viewport_scene_view> viewport_scene_view = m_context.scene_views->create_viewport_scene_view(
        m_context.scene_views->get_viewport_config_data(),
        *m_context.graphics_device,
        *m_context.rendergraph,
        *m_context.imgui_windows,
        *m_context.app_rendering,
        *m_context.app_settings,
        *m_context.post_processing,
        *m_context.tools,
        scene_name,
        scene_root,
        camera,
        m_context.app_settings->graphics.current_graphics_preset.msaa_sample_count,
        rendergraph_output_node
    );
    m_context.scene_views->create_viewport_window(
        *m_context.imgui_renderer,
        *m_context.imgui_windows,
        *m_context.app_message_bus,
        viewport_scene_view,
        rendergraph_output_node,
        scene_name,
        ""
    );

    // Attach the global tools (Hud / Hotbar / OpenXR Headset_view) to this
    // scene if none existed yet (e.g. a --no-scene startup). on_scene_created()
    // ignores this once the tools are already homed in an earlier scene.
    m_context.app_message_bus->scene_created.send_message(Scene_created_message{scene_root});

    log_scene->info("Created new scene '{}'", scene_name);
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

    // visible: inert while the node is empty, but attachments added later
    // (Mesh, Light, Geometry_graph_mesh, ...) sync their visibility from
    // the node - without it anything attached to an "empty" node would be
    // invisibly stuck.
    auto new_empty_node = std::make_shared<erhe::scene::Node>("new empty node");
    new_empty_node->enable_flag_bits(Item_flags::content | Item_flags::visible | Item_flags::show_in_ui);
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

auto Scene_commands::create_new_layout(erhe::scene::Node* parent) -> std::shared_ptr<erhe::scene::Layout>
{
    Scene_root* scene_root = get_scene_root(parent);
    if (scene_root == nullptr) {
        return {};
    }

    auto new_node   = std::make_shared<erhe::scene::Node>("new layout node");
    auto new_layout = std::make_shared<erhe::scene::Layout>("new layout");
    new_node  ->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    new_layout->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui | Item_flags::show_debug_visualizations);
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
                    std::make_shared<Node_attach_operation>(new_layout, new_node)
                }
            }
        )
    );

    return new_layout;
}

auto Scene_commands::create_new_rigid_body(erhe::scene::Node* node) -> std::shared_ptr<Node_physics>
{
    std::shared_ptr<erhe::scene::Node> target = (node != nullptr)
        ? std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this())
        : m_context.selection->get_last_selected<erhe::scene::Node>();

    if (!target) {
        // Nothing to attach to: create a new empty node carrying the body.
        Scene_root* scene_root = get_scene_root(static_cast<erhe::scene::Node*>(nullptr));
        if (scene_root == nullptr) {
            return {};
        }
        auto new_node = std::make_shared<erhe::scene::Node>("new rigid body node");
        new_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

        erhe::physics::IRigid_body_create_info create_info{};
        create_info.collision_shape = erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
        create_info.debug_label     = new_node->get_name();
        auto node_physics = std::make_shared<Node_physics>(create_info);
        node_physics->set_name("new rigid body");
        node_physics->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

        m_context.operation_stack->queue(
            std::make_shared<Compound_operation>(
                Compound_operation::Parameters{
                    .operations = {
                        std::make_shared<Item_insert_remove_operation>(
                            Item_insert_remove_operation::Parameters{
                                .context = m_context,
                                .item    = new_node,
                                .parent  = scene_root->get_hosted_scene()->get_root_node(),
                                .mode    = Item_insert_remove_operation::Mode::insert
                            }
                        ),
                        std::make_shared<Node_attach_operation>(node_physics, new_node)
                    }
                }
            )
        );
        return node_physics;
    }

    erhe::physics::IRigid_body_create_info create_info{};
    create_info.collision_shape = build_shape_from_node_mesh(target.get(), true);
    if (!create_info.collision_shape) {
        create_info.collision_shape = erhe::physics::ICollision_shape::create_box_shape_shared(glm::vec3{0.5f});
    }
    return create_new_rigid_body(target.get(), create_info);
}

auto Scene_commands::create_new_rigid_body(erhe::scene::Node* node, const erhe::physics::IRigid_body_create_info& create_info) -> std::shared_ptr<Node_physics>
{
    if (node == nullptr) {
        return {};
    }
    if (erhe::scene::get_attachment<Node_physics>(node)) {
        log_scene->warn("Node '{}' already has a rigid body attachment", node->get_name());
        return {};
    }

    erhe::physics::IRigid_body_create_info effective_create_info = create_info;
    if (effective_create_info.debug_label.empty()) {
        effective_create_info.debug_label = node->get_name();
    }
    auto node_physics = std::make_shared<Node_physics>(effective_create_info);
    node_physics->set_name("new rigid body");
    node_physics->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

    m_context.operation_stack->queue(
        std::make_shared<Node_attach_operation>(
            node_physics,
            std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this())
        )
    );
    return node_physics;
}

auto Scene_commands::create_new_joint(
    erhe::scene::Node*                                            node,
    const std::shared_ptr<erhe::scene::Node>&                     connected_node,
    const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings,
    const bool                                                    enable_collision
) -> std::shared_ptr<Node_joint>
{
    std::shared_ptr<erhe::scene::Node> target;
    std::shared_ptr<erhe::scene::Node> connected = connected_node;
    if (node != nullptr) {
        target = std::static_pointer_cast<erhe::scene::Node>(node->shared_from_this());
    } else {
        target = m_context.selection->get_last_selected<erhe::scene::Node>();
        if (target && !connected) {
            // Convenience for the bare command: connect to another selected
            // node in the same scene, when there is one.
            for (const std::shared_ptr<erhe::Item_base>& item : m_context.selection->get_selected_items()) {
                const std::shared_ptr<erhe::scene::Node> other = std::dynamic_pointer_cast<erhe::scene::Node>(item);
                if (other && (other != target) && (other->get_item_host() == target->get_item_host())) {
                    connected = other;
                    break;
                }
            }
        }
    }

    auto node_joint = std::make_shared<Node_joint>(connected, settings, enable_collision);
    node_joint->set_name("new joint");
    node_joint->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);

    if (target) {
        m_context.operation_stack->queue(std::make_shared<Node_attach_operation>(node_joint, target));
        return node_joint;
    }

    // Nothing to attach to: create a new empty node carrying the joint.
    Scene_root* scene_root = get_scene_root(static_cast<erhe::scene::Node*>(nullptr));
    if (scene_root == nullptr) {
        return {};
    }
    auto new_node = std::make_shared<erhe::scene::Node>("new joint node");
    new_node->enable_flag_bits(Item_flags::content | Item_flags::show_in_ui);
    m_context.operation_stack->queue(
        std::make_shared<Compound_operation>(
            Compound_operation::Parameters{
                .operations = {
                    std::make_shared<Item_insert_remove_operation>(
                        Item_insert_remove_operation::Parameters{
                            .context = m_context,
                            .item    = new_node,
                            .parent  = scene_root->get_hosted_scene()->get_root_node(),
                            .mode    = Item_insert_remove_operation::Mode::insert
                        }
                    ),
                    std::make_shared<Node_attach_operation>(node_joint, new_node)
                }
            }
        )
    );
    return node_joint;
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

    ERHE_VERIFY(m_context.current_command_buffer != nullptr);
    erhe::graphics::Command_buffer& command_buffer = *m_context.current_command_buffer;

    // Rendertarget_mesh is Mesh (can be rendered in 3D scene) with textured rectangle vertex data, provides Texture
    auto mesh = std::make_shared<Rendertarget_mesh>(
        *m_context.graphics_device,
        command_buffer,
        *m_context.mesh_memory,
        2048,
        2048,
        600.0f
    );

    {
        // Clear once
        erhe::graphics::Render_command_encoder render_encoder = m_context.graphics_device->make_render_command_encoder(command_buffer);
        erhe::graphics::Scoped_render_pass scoped_render_pass{*mesh->get_render_pass(), command_buffer};
        mesh->render_done(command_buffer, m_context);
    }

    mesh->layer_id = scene_root->layers().rendertarget()->id;
    mesh->enable_flag_bits(erhe::Item_flags::rendertarget | erhe::Item_flags::visible | erhe::Item_flags::show_in_ui);

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

    // Rendertarget_imgui_host is host for ImGui windows, drawing into the mesh
    // texture (scene-mesh path). It is also a rendergraph node. The host renders
    // through a Rendertarget_view; for a standalone scene rendertarget that view
    // is a Mesh_rendertarget_view wrapping the mesh and node.
    auto rendertarget_view = std::make_shared<Mesh_rendertarget_view>(mesh, node);
    auto rendertarget_imgui_host = std::make_shared<Rendertarget_imgui_host>(
        *m_context.imgui_renderer,
        *m_context.rendergraph,
        m_context,
        *rendertarget_view,
        "Rendertarget ImGui host",
        true
    );
    // Unless the shared_ptrs are kept somewhere, these get destroyed as soon as
    // the scope is exited. TODO This is a temp hack, figure out who should be
    // the owner.
    m_keep_alive_views.push_back(rendertarget_view);
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
        m_context.scene_views->get_viewport_config_data(),
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
        *m_context.app_message_bus,
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
