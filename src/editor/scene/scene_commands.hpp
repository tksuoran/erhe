#pragma once

#include "config/generated/add_cameras_args.hpp"
#include "config/generated/add_lights_args.hpp"
#include "config/generated/add_room_args.hpp"
#include "scene/make_mesh_config.hpp"

#include "erhe_commands/command.hpp"

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
namespace erhe::primitive {
    class Material;
}
namespace erhe::rendergraph {
    class Rendergraph;
}
namespace erhe::scene {
    class Camera;
    class Light;
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class Headset_view;
class Mesh_rendertarget_view;
class Operation_stack;
class Rendertarget_mesh;
class Rendertarget_imgui_host;
class Scene_commands;
class Scene_root;
class Selection_tool;
class Scene_views;

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
    Scene_commands(erhe::commands::Commands& commands, App_context& context);

    // Public API
    auto create_new_camera      (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_empty_node  (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Node>;
    auto create_new_light       (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Light>;
    auto create_new_rendertarget(erhe::scene::Node* parent = nullptr) -> std::shared_ptr<Rendertarget_mesh>;
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

    Create_new_camera_command       m_create_new_camera_command;
    Create_new_empty_node_command   m_create_new_empty_node_command;
    Create_new_light_command        m_create_new_light_command;
    Create_new_rendertarget_command m_create_new_rendertarget_command;
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
