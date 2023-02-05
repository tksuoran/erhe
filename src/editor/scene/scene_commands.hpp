#pragma once

#include "erhe/application/commands/command.hpp"
#include "erhe/components/components.hpp"

#include <memory>
#include <string_view>
#include <vector>

class btCollisionShape;


namespace erhe::scene
{
    class Camera;
    class Light;
    class Node;
}

namespace editor
{

class Scene_root;

class Create_new_camera_command
    : public erhe::application::Command
{
public:
    Create_new_camera_command();
    auto try_call() -> bool override;
};

class Create_new_empty_node_command
    : public erhe::application::Command
{
public:
    Create_new_empty_node_command();
    auto try_call() -> bool override;
};

class Create_new_light_command
    : public erhe::application::Command
{
public:
    Create_new_light_command();
    auto try_call() -> bool override;
};

class Scene_commands
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Scene_commands"};
    static constexpr uint32_t         c_type_hash = compiletime_xxhash::xxh32(c_type_name.data(), c_type_name.size(), {});

    Scene_commands ();
    ~Scene_commands() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }
    void declare_required_components() override;
    void initialize_component       () override;

    // Public API
    auto create_new_camera    (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_empty_node(erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Node>;
    auto create_new_light     (erhe::scene::Node* parent = nullptr) -> std::shared_ptr<erhe::scene::Light>;
    auto get_scene_root       (erhe::scene::Node* parent) const -> Scene_root*;

private:
    // Commands
    Create_new_camera_command     m_create_new_camera_command;
    Create_new_empty_node_command m_create_new_empty_node_command;
    Create_new_light_command      m_create_new_light_command;
};

extern Scene_commands* g_scene_commands;

} // namespace editor
