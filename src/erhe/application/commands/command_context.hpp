#pragma once

#include <glm/glm.hpp>

namespace erhe::application {

class Command;
class Imgui_window;
class View;
///class Viewport_window;

class Command_context
{
public:
    explicit Command_context(
        View&         view,
        Imgui_window* window              = nullptr,
        glm::dvec2    vec2_absolute_value = glm::dvec2{0.0f, 0.0},
        glm::dvec2    vec2_relative_value = glm::dvec2{0.0f, 0.0}
    );

    [[nodiscard]] auto get_window             () -> Imgui_window*;
    [[nodiscard]] auto get_vec2_absolute_value() -> glm::dvec2;
    [[nodiscard]] auto get_vec2_relative_value() -> glm::dvec2;
    //[[nodiscard]] auto hovering_over_gui   () -> bool;

    [[nodiscard]] auto accept_mouse_command(Command* const command) -> bool;
    //[[nodiscard]] auto absolute_pointer    () const -> glm::dvec2;
    //[[nodiscard]] auto relative_pointer    () const -> glm::dvec2;
    //[[nodiscard]] auto relative_wheel      () const -> glm::dvec2;
    [[nodiscard]] auto view                () const -> View&;

private:
    View&         m_view;
    Imgui_window* m_window             {nullptr};
    glm::dvec2    m_vec2_absolute_value{0.0f, 0.0};
    glm::dvec2    m_vec2_relative_value{0.0f, 0.0};
};

} // namespace erhe::application

