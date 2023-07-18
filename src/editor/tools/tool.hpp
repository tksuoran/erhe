#pragma once

#include "erhe/commands/command.hpp"

#include <glm/glm.hpp>

#include <optional>

namespace editor
{

class Editor_context;
class Editor_message;
class Render_context;
class Scene_view;
class Tools;

class Tool_flags
{
public:
    static constexpr uint64_t none            = 0u;
    static constexpr uint64_t enabled         = (1u << 0);
    static constexpr uint64_t background      = (1u << 1);
    static constexpr uint64_t toolbox         = (1u << 2);
    static constexpr uint64_t secondary       = (1u << 3);
    static constexpr uint64_t allow_secondary = (1u << 4);
};

class Tool
    : public erhe::commands::Command_host
{
public:
    explicit Tool(Editor_context& editor_context);

    // Implements Command_host
    [[nodiscard]] auto get_priority() const -> int override;

    virtual void tool_render           (const Render_context& context) { static_cast<void>(context); }
    virtual void cancel_ready          () {}
    virtual void tool_properties       () {}
    virtual void handle_priority_update(int old_priority, int new_priority)
    {
        static_cast<void>(old_priority);
        static_cast<void>(new_priority);
    }

    [[nodiscard]] auto get_base_priority        () const -> int;
    [[nodiscard]] auto get_priority_boost       () const -> int;
    [[nodiscard]] auto get_hover_scene_view     () const -> Scene_view*;
    [[nodiscard]] auto get_last_hover_scene_view() const -> Scene_view*;
    [[nodiscard]] auto get_flags                () const -> uint64_t;
    [[nodiscard]] auto get_icon                 () const -> std::optional<glm::vec2>;
    void set_priority_boost(int priority_boost);

protected:
    void on_message          (Editor_message& message);
    void set_base_priority   (int base_priority);
    void set_flags           (uint64_t flags);
    void set_icon            (glm::vec2 icon);
    void set_hover_scene_view(Scene_view* scene_view);

    Editor_context& m_context;

private:
    int                      m_base_priority     {0};
    int                      m_priority_boost    {0};
    uint64_t                 m_flags             {0};
    std::optional<glm::vec2> m_icon;
    Scene_view*              m_hover_scene_view     {nullptr};
    Scene_view*              m_last_hover_scene_view{nullptr};
};

} // namespace editor
