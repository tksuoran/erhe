#include "erhe/ui/gui_renderer.hpp"

namespace erhe::ui
{

std::shared_ptr<context> context::s_current_context;

context::context()
{
}

context::~context()
{
}

auto context::current()
-> std::shared_ptr<context>
{
    return s_current_context;
}

void context::deinitialize()
{
    s_current_context.reset();
}

void context::make_current(std::shared_ptr<context> context)
{
    s_current_context = context;
}

auto context::gui_renderer()
-> std::shared_ptr<Gui_renderer>
{
    if (!s_gui_renderer)
    {
        s_gui_renderer = std::make_shared<Gui_renderer>();
    }
    return s_gui_renderer;
}

auto context::style()
-> context_style&
{
    return m_style;
}

} // namespace erhe::ui
