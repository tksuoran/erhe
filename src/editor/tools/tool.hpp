#pragma once

namespace editor
{

class Editor_message;
class Render_context;
class Scene_view;

class Tool
{
public:
    [[nodiscard]] virtual auto description  () -> const char* = 0;
    [[nodiscard]] virtual auto tool_priority() const -> int { return 999; }

    virtual void tool_render            (const Render_context& context) { static_cast<void>(context); }
    virtual void cancel_ready           () {}
    virtual void tool_properties        () {}
    virtual void on_enable_state_changed() {}

    [[nodiscard]] auto is_enabled    () const -> bool;
    [[nodiscard]] auto get_scene_view() const -> Scene_view*;
    void set_enable_state(const bool enable_state);

protected:
    void on_message(Editor_message& message);

private:
    bool        m_enabled   {true};
    Scene_view* m_scene_view{nullptr};
};

} // namespace editor
