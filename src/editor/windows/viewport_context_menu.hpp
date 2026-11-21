#pragma once

#include "erhe_commands/command.hpp"

#include <glm/glm.hpp>

#include <memory>
#include <optional>

namespace erhe {
    class Item_base;
}
namespace erhe::commands {
    class Commands;
}

namespace editor {

class App_context;
class Scene_root;
class Viewport_scene_view;

class Viewport_context_menu_command : public erhe::commands::Command
{
public:
    Viewport_context_menu_command(erhe::commands::Commands& commands, App_context& context);

    void try_ready()         override;
    auto try_call () -> bool override;

private:
    App_context& m_context;
};

// The context menu of a viewport: the Scene Hierarchy window's menu, opened by a right click
// that is not a drag (doc/editor/windows.md "Viewport context menu").
class Viewport_context_menu : public erhe::commands::Command_host
{
public:
    Viewport_context_menu(erhe::commands::Commands& commands, App_context& context);

    // Commands
    [[nodiscard]] auto try_ready() const -> bool;
    auto open() -> bool;

    // Submits the popup; called by the viewport's window inside its ImGui window.
    void imgui(const Viewport_scene_view& viewport_scene_view);

private:
    [[nodiscard]] auto get_hover_scene_view() const -> std::shared_ptr<Viewport_scene_view>;
    void reset();

    Viewport_context_menu_command      m_command;
    App_context&                       m_context;

    // Weak: an open menu keeps neither its scene nor its item alive.
    std::weak_ptr<Viewport_scene_view> m_viewport_scene_view;
    std::weak_ptr<Scene_root>          m_scene_root;
    std::weak_ptr<erhe::Item_base>     m_item; // expired with m_has_item false: the empty area's menu
    bool                               m_has_item    {false};
    bool                               m_open_pending{false};
    bool                               m_shown       {false};
    std::optional<glm::vec3>           m_world_position;
};

}
