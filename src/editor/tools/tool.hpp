#pragma once

#include "windows/window.hpp"
#include "erhe/scene/viewport.hpp"

namespace sample
{

struct Pointer_context;
class Line_renderer;
class Scene_manager;
class Text_renderer;

struct Render_context
{
    Pointer_context*      pointer_context{nullptr};
    Scene_manager*        scene_manager  {nullptr};
    Line_renderer*        line_renderer  {nullptr};
    Text_renderer*        text_renderer  {nullptr};
    erhe::scene::Viewport viewport       {0, 0, 0, 0};
    double                time           {0.0};
};

class Tool
    : public Window
{
public:
    enum class State : unsigned int
    {
        disabled = 0,
        passive,
        ready,
        active
    };

    virtual auto name() -> const char* = 0;

    // Interaction with pointer
    virtual auto update(Pointer_context&) -> bool
    {
        // If derived does not implement update,
        // event will not be consumed.
        return false;
    }

    // Visual rendering of the tool.
    virtual void render_update() {}

    virtual void render(Render_context&) {}

    virtual auto state() const -> State = 0;

    virtual void cancel_ready() {}
};

} // namespace sample
