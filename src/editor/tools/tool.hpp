#pragma once

#include "erhe/scene/viewport.hpp"

namespace editor
{

class Line_renderer;
class Pointer_context;
class Render_context;
class Scene_manager;
class Text_renderer;

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
    virtual auto update(Pointer_context&) -> bool
    {
        // If derived does not implement update,
        // event will not be consumed.
        return false;
    }

    // Visual rendering of the tool.
    virtual void render_update  (const Render_context&) {}
    virtual void render         (const Render_context&) {}
    virtual auto state          () const -> State = 0;
    virtual void cancel_ready   () {}
    virtual void tool_properties() {}
};

} // namespace editor
