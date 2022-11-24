#pragma once

#include <memory>

namespace editor
{

class Viewport_window;

enum class Editor_event_type : int
{
    selection_changed,
    viewport_changed,
    graphics_settings_changed
};

class Editor_message
{
public:
    Editor_event_type                event_type;
    std::shared_ptr<Viewport_window> old_viewport_window;
    std::shared_ptr<Viewport_window> new_viewport_window;
};

}
