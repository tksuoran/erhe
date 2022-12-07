#pragma once

#include <glm/glm.hpp>

namespace erhe::application {

class Command;
class Commands;
class Imgui_window;
class Scene_view;
class View;

class Command_context
{
public:
    explicit Command_context(
        Commands&  commands,
        uint32_t   button_bits         = 0u,
        glm::dvec2 vec2_absolute_value = glm::dvec2{0.0, 0.0},
        glm::dvec2 vec2_relative_value = glm::dvec2{0.0, 0.0}
    );

    [[nodiscard]] auto get_button_bits        () const -> uint32_t;
    [[nodiscard]] auto get_vec2_absolute_value() const -> glm::dvec2;
    [[nodiscard]] auto get_vec2_relative_value() const -> glm::dvec2;
    [[nodiscard]] auto accept_mouse_command   (Command* const command) -> bool;
    [[nodiscard]] auto commands               () const -> Commands&;

private:
    Commands&  m_commands;
    uint32_t   m_button_bits        {0u};
    glm::dvec2 m_vec2_absolute_value{0.0, 0.0};
    glm::dvec2 m_vec2_relative_value{0.0, 0.0};
};

} // namespace erhe::application

