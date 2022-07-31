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

class Editor_scenes;
class Scene_commands;

class Create_new_camera_command
    : public erhe::application::Command
{
public:
    explicit Create_new_camera_command(Scene_commands& scene_commands)
        : Command         {"scene.create_new_camera"}
        , m_scene_commands{scene_commands}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_commands& m_scene_commands;
};

class Create_new_empty_node_command
    : public erhe::application::Command
{
public:
    explicit Create_new_empty_node_command(Scene_commands& scene_commands)
        : Command         {"scene.create_new_empty_node"}
        , m_scene_commands{scene_commands}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_commands& m_scene_commands;
};

class Create_new_light_command
    : public erhe::application::Command
{
public:
    explicit Create_new_light_command(Scene_commands& scene_commands)
        : Command         {"scene.create_new_light"}
        , m_scene_commands{scene_commands}
    {
    }

    auto try_call(erhe::application::Command_context& context) -> bool override;

private:
    Scene_commands& m_scene_commands;
};

class Scene_commands
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Scene_commands"};
    static constexpr uint32_t         hash = compiletime_xxhash::xxh32(c_label.data(), c_label.size(), {});

    Scene_commands ();
    ~Scene_commands() noexcept override;

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }
    void declare_required_components() override;
    void initialize_component       () override;
    void post_initialize            () override;

    // Public API
    auto create_new_camera    () -> std::shared_ptr<erhe::scene::Camera>;
    auto create_new_empty_node() -> std::shared_ptr<erhe::scene::Node>;
    auto create_new_light     () -> std::shared_ptr<erhe::scene::Light>;

private:
    // Commands
    Create_new_camera_command     m_create_new_camera_command;
    Create_new_empty_node_command m_create_new_empty_node_command;
    Create_new_light_command      m_create_new_light_command;

    // Component dependencies
    std::shared_ptr<Editor_scenes> m_editor_scenes;
};

} // namespace editor
