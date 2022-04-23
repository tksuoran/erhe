#include "erhe/application/commands/command_context.hpp"
#include "erhe/application/commands/command.hpp"
#include "erhe/application/view.hpp"

namespace erhe::application
{

Command_context::Command_context(
    View&         view,
    Imgui_window* window,
    glm::dvec2    vec2_absolute_value,
    glm::dvec2    vec2_relative_value
)
    : m_view               {view}
    , m_window             {window}
    , m_vec2_absolute_value{vec2_absolute_value}
    , m_vec2_relative_value{vec2_relative_value}
{
}

auto Command_context::get_window() -> Imgui_window*
{
    return m_window;
}

auto Command_context::get_vec2_absolute_value() -> glm::dvec2
{
    return m_vec2_absolute_value;
}

auto Command_context::get_vec2_relative_value() -> glm::dvec2
{
    return m_vec2_relative_value;
}

auto Command_context::accept_mouse_command(Command* command) -> bool
{
    return m_view.accept_mouse_command(command);
}

auto Command_context::view() const -> View&
{
    return m_view;
}

} // namespace erhe::application

