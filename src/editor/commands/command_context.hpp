#pragma once

#include <glm/glm.hpp>

namespace editor {

class Command;
class Editor_view;
class Pointer_context;
class Viewport_window;

class Command_context
{
public:
    Command_context(
        Editor_view&      editor_view,
        const glm::dvec2& absolute_pointer = glm::dvec2{0.0, 0.0},
        const glm::dvec2& relative_pointer = glm::dvec2{0.0, 0.0}
    );

    [[nodiscard]] auto viewport_window     () -> Viewport_window*;
    [[nodiscard]] auto hovering_over_tool  () -> bool;
    [[nodiscard]] auto hovering_over_gui   () -> bool;

    [[nodiscard]] auto accept_mouse_command(Command* const command) -> bool;
    [[nodiscard]] auto absolute_pointer    () const -> glm::dvec2;
    [[nodiscard]] auto relative_pointer    () const -> glm::dvec2;
    [[nodiscard]] auto editor_view         () const -> Editor_view&;

private:
    Editor_view& m_editor_view;
    glm::dvec2   m_absolute_pointer;
    glm::dvec2   m_relative_pointer;
};

} // namespace Editor

