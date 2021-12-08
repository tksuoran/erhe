#pragma once

#include "command.hpp" // state
#include "erhe/toolkit/view.hpp" // keycode

#include <glm/glm.hpp>

#include <optional>

namespace editor
{

class Render_context;

class Tool
{
public: 
    virtual [[nodiscard]] auto description  () -> const char* = 0;
    virtual [[nodiscard]] auto tool_priority() const -> int { return 999; }

    virtual void begin_frame            () {}
    virtual void tool_render            (const Render_context& context) { static_cast<void>(context); }
    virtual void cancel_ready           () {}
    virtual void tool_properties        () {}
    virtual void on_enable_state_changed() {}

    void set_enable_state(const bool enable_state)
    {
        m_enabled = enable_state;
        on_enable_state_changed();
    };

    auto is_enabled() const -> bool
    {
        return m_enabled;
    }

private:
    bool m_enabled{true};
};

} // namespace editor
