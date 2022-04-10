#include "commands/command_context.hpp"
#include "commands/command.hpp"
#include "tools/pointer_context.hpp"
#include "editor_view.hpp"

namespace editor {

Command_context::Command_context(
    Editor_view&      editor_view,
    const glm::dvec2& absolute_pointer,
    const glm::dvec2& relative_pointer

)
    : m_editor_view     {editor_view}
    , m_absolute_pointer{absolute_pointer}
    , m_relative_pointer{relative_pointer}
{
}

auto Command_context::viewport_window() -> Viewport_window*
{
    return m_editor_view.pointer_context()->window();
}

auto Command_context::hovering_over_tool() -> bool
{
    return m_editor_view.pointer_context()->get_hover(Pointer_context::tool_slot).valid;
}

auto Command_context::hovering_over_gui() -> bool
{
    return m_editor_view.pointer_context()->get_hover(Pointer_context::gui_slot).valid;
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

