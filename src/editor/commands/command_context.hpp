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
    explicit Command_context(Editor_view& editor_view);

    [[nodiscard]] auto viewport_window     () -> Viewport_window*;
    [[nodiscard]] auto hovering_over_tool  () -> bool;
    [[nodiscard]] auto hovering_over_gui   () -> bool;

    [[nodiscard]] auto accept_mouse_command(Command* const command) -> bool;
    [[nodiscard]] auto absolute_pointer    () const -> glm::dvec2;
    [[nodiscard]] auto relative_pointer    () const -> glm::dvec2;
    [[nodiscard]] auto relative_wheel      () const -> glm::dvec2;
    [[nodiscard]] auto editor_view         () const -> Editor_view&;

private:
    Editor_view& m_editor_view;
};

} // namespace Editor

