#pragma once

#include "windows/window.hpp"
#include "erhe/scene/viewport.hpp"

namespace editor
{

class  Line_renderer;
struct Pointer_context;
struct Render_context;
class  Scene_manager;
class  Text_renderer;

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

    virtual auto description() -> const char* = 0;

    // Interaction with pointer
    virtual auto update(Pointer_context&) -> bool
    {
        // If derived does not implement update,
        // event will not be consumed.
        return false;
    }

    // Visual rendering of the tool.
    virtual void render_update(const Render_context&) {}
    virtual void render       (const Render_context&) {}
    virtual auto state        () const -> State = 0;
    virtual void cancel_ready () {}
};

} // namespace editor
