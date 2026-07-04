#pragma once

#include "config/generated/add_cameras_args.hpp"
#include "config/generated/add_lights_args.hpp"
#include "config/generated/add_room_args.hpp"
#include "scene/make_mesh_config.hpp"

#include "app_message.hpp"

#include "erhe_commands/command.hpp"
#include "erhe_message_bus/message_bus.hpp"

#include <memory>
#include <string_view>
#include <vector>

struct Make_mesh_args;

class btCollisionShape;

namespace erhe::commands {
    class Commands;
}
namespace erhe::imgui {
    class Imgui_renderer;
    class Imgui_windows;
}
namespace erhe::physics {
    class IRigid_body_create_info;
    class Physics_joint_settings;
}
namespace erhe::primitive {
    class Material;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Layout;
    class Light;
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class App_message_bus;
class Headset_view;
class Mesh_rendertarget_view;
class Node_joint;
class Node_physics;
class Operation_stack;
class Rendertarget_mesh;
class Rendertarget_imgui_host;
class Scene_commands;
class Scene_root;
class Selection_tool;
class Scene_views;

class Create_new_scene_command : public erhe::commands::Command
{
public:
    Create_new_scene_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_camera_command : public erhe::commands::Command
{
public:
    Create_new_camera_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_empty_node_command : public erhe::commands::Command
{
public:
    Create_new_empty_node_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_light_command : public erhe::commands::Command
{
public:
    Create_new_light_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_rendertarget_command : public erhe::commands::Command
{
public:
    Create_new_rendertarget_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_layout_command : public erhe::commands::Command
{
public:
    Create_new_layout_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_rigid_body_command : public erhe::commands::Command
{
public:
    Create_new_rigid_body_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Create_new_joint_command : public erhe::commands::Command
{
public:
    Create_new_joint_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;

private:
    App_context& m_context;
};

class Add_cameras_command : public erhe::commands::Command
{
public:
    Add_cameras_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_cameras_args& args);

private:
    App_context&     m_context;
    Add_cameras_args m_args{};
};

class Add_room_command : public erhe::commands::Command
{
public:
    Add_room_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_room_args& args);

private:
    App_context&  m_context;
    Add_room_args m_args{};
};

class Add_lights_command : public erhe::commands::Command
{
public:
    Add_lights_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void apply_args(const Add_lights_args& args);

private:
    App_context&    m_context;
    Add_lights_args m_args{};
};

class Add_platonic_solids_command : public erhe::commands::Command
{
public:
    Add_platonic_solids_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_johnson_solids_command : public erhe::commands::Command
{
public:
    Add_johnson_solids_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_curved_shapes_command : public erhe::commands::Command
{
public:
    Add_curved_shapes_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_chain_command : public erhe::commands::Command
{
public:
    Add_chain_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Add_toruses_command : public erhe::commands::Command
{
public:
    Add_toruses_command(erhe::commands::Commands& commands, App_context& context);
    auto try_call() -> bool override;
    void set_make_mesh_config(const Make_mesh_config& config);
    void apply_args             (const Make_mesh_args&  args);

private:
    App_context&     m_context;
    Make_mesh_config m_make_mesh_config{};
};

class Scene_commands
{
public:
    Scene_commands(erhe::commands::Commands& commands, App_context& context, App_message_bus& app_message_bus);

    // Public API

    // Creates a new empty scene: a fresh Scene_root with its own (empty)
    // content library, holding just a default camera -- no lights, no meshes.
    // The scene is registered to the editor scene list, given its browser
    // window and a viewport window looking through the camera, and announced
    // via Scene_created_message. Not undoable (like loading a scene).
    //
    // Registers ImGui windows, so this must NOT be called from inside ImGui
    // window iteration (e.g. from a menu); the scene.create_new_scene command
    // queues Create_scene_message and the actual creation runs from the
    // message bus pump.
    auto create_new_scene       () -> std::shared_ptr<Scene_root>;
    auto create_new_camera      (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_empty_node  (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Node>;
    auto create_new_light       (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Light>;
    auto create_new_layout      (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Layout>;
    auto create_new_rendertarget(erhe::scene::Node* parent = nullptr) -> std::shared_ptr<Rendertarget_mesh>;

    // Attaches a new Node_physics to the node (undoable). With no node, uses
    // the last selected node; with no selection, creates a new empty node
    // with the body. The default create info is a dynamic body of mass 1
    // with a convex hull shape built from the node's mesh (a unit box when
    // the node has no usable mesh geometry). Returns empty when the node
    // already has a Node_physics (one rigid body per node).
    auto create_new_rigid_body(erhe::scene::Node* node = nullptr) -> std::shared_ptr<Node_physics>;
    auto create_new_rigid_body(erhe::scene::Node* node, const erhe::physics::IRigid_body_create_info& create_info) -> std::shared_ptr<Node_physics>;

    // Attaches a new Node_joint to the node (undoable), joining the nearest
    // self-or-ancestor rigid body of the node to that of connected_node (no
    // connected node = the world). Settings may be empty (a free six-dof
    // joint); assign shared Physics_joint_settings later in the properties.
    // With no node, uses the last selected node; with no selection, creates
    // a new empty node with the joint.
    auto create_new_joint(
        erhe::scene::Node*                                            node             = nullptr,
        const std::shared_ptr<erhe::scene::Node>&                     connected_node   = {},
        const std::shared_ptr<erhe::physics::Physics_joint_settings>& settings         = {},
        bool                                                          enable_collision = false
    ) -> std::shared_ptr<Node_joint>;

    auto get_scene_root         (erhe::scene::Node* parent) const -> Scene_root*;
    auto get_scene_root         (erhe::primitive::Material* material) const -> Scene_root*;

    [[nodiscard]] auto get_add_cameras_command        () -> Add_cameras_command&;
    [[nodiscard]] auto get_add_room_command           () -> Add_room_command&;
    [[nodiscard]] auto get_add_lights_command         () -> Add_lights_command&;
    [[nodiscard]] auto get_add_platonic_solids_command() -> Add_platonic_solids_command&;
    [[nodiscard]] auto get_add_johnson_solids_command () -> Add_johnson_solids_command&;
    [[nodiscard]] auto get_add_curved_shapes_command  () -> Add_curved_shapes_command&;
    [[nodiscard]] auto get_add_chain_command          () -> Add_chain_command&;
    [[nodiscard]] auto get_add_toruses_command        () -> Add_toruses_command&;

private:
    App_context& m_context;

    erhe::message_bus::Subscription<Create_scene_message> m_create_scene_subscription;

    Create_new_scene_command        m_create_new_scene_command;
    Create_new_camera_command       m_create_new_camera_command;
    Create_new_empty_node_command   m_create_new_empty_node_command;
    Create_new_light_command        m_create_new_light_command;
    Create_new_layout_command       m_create_new_layout_command;
    Create_new_rendertarget_command m_create_new_rendertarget_command;
    Create_new_rigid_body_command   m_create_new_rigid_body_command;
    Create_new_joint_command        m_create_new_joint_command;
    Add_cameras_command             m_add_cameras_command;
    Add_room_command                m_add_room_command;
    Add_lights_command              m_add_lights_command;
    Add_platonic_solids_command     m_add_platonic_solids_command;
    Add_johnson_solids_command      m_add_johnson_solids_command;
    Add_curved_shapes_command       m_add_curved_shapes_command;
    Add_chain_command               m_add_chain_command;
    Add_toruses_command             m_add_toruses_command;

    // TODO Figure out who should have ownership of these. The views are
    // declared before the hosts so that, at destruction, the hosts (which hold
    // a non-owning Rendertarget_view*) are destroyed before the views.
    std::vector<std::shared_ptr<Mesh_rendertarget_view>>  m_keep_alive_views;
    std::vector<std::shared_ptr<Rendertarget_imgui_host>> m_keep_alive;
};

}
