#pragma once

#include "erhe_commands/command.hpp"

#include <memory>
#include <string_view>
#include <vector>

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

private:
    App_context& m_context;

    Create_new_camera_command       m_create_new_camera_command;
    Create_new_empty_node_command   m_create_new_empty_node_command;
    Create_new_light_command        m_create_new_light_command;
    Create_new_rendertarget_command m_create_new_rendertarget_command;

    // TODO Figure out who should have ownership of these
    std::vector<std::shared_ptr<Rendertarget_imgui_host>> m_keep_alive;
};

} // namespace editor
