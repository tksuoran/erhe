#include "commands/command_context.hpp"
#include "commands/command.hpp"
#include "tools/pointer_context.hpp"
#include "editor_view.hpp"

namespace editor {

Command_context::Command_context(
    Editor_view&      editor_view,
    Pointer_context&  pointer_context,
    const glm::dvec2& absolute_pointer,
    const glm::dvec2& relative_pointer

)
    : m_editor_view     {editor_view}
    , m_pointer_context {pointer_context}
    , m_absolute_pointer{absolute_pointer}
    , m_relative_pointer{relative_pointer}
{
}

auto Command_context::viewport_window() -> Viewport_window*
{
    return m_pointer_context.window();
}

auto Command_context::hovering_over_tool() -> bool
{
    return m_pointer_context.hovering_over_tool();
}

auto Command_context::hovering_over_gui() -> bool
{
    return m_pointer_context.hovering_over_gui();
}

auto Command_context::accept_mouse_command(Command* command) -> bool
{
    return m_editor_view.accept_mouse_command(command);
}

auto Command_context::absolute_pointer() const -> glm::dvec2
{
    return m_absolute_pointer;
}

auto Command_context::relative_pointer() const -> glm::dvec2
{
    return m_relative_pointer;
}

auto Command_context::editor_view() const -> Editor_view&
{
    return m_editor_view;
}

} // namespace Editor

