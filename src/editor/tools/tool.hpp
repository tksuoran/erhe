#pragma once

namespace editor
{

class Render_context;

class Tool
{
public:
    enum class State : unsigned int
    {
        Disabled = 0,
        Passive,
        Ready,
        Active
    };

    static constexpr const char* c_state_str[] =
    {
        "disabled",
        "passive",
        "ready",
        "active"
    };

    virtual auto description() -> const char* = 0;

    // Interaction with pointer
    virtual auto tool_update() -> bool
    {
        // If derived does not implement update,
        // event will not be consumed.
        return false;
    }

    // Visual rendering of the tool.
    virtual void begin_frame    () {}
    virtual void tool_render    (const Render_context& context) { static_cast<void>(context); }
    virtual auto state          () const -> State = 0;
    virtual void cancel_ready   () {}
    virtual void tool_properties() {}
};

} // namespace editor
