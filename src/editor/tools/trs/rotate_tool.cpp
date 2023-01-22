#include "tools/trs/rotate_tool.hpp"

#include "graphics//icon_set.hpp"
#include "tools/tools.hpp"
#include "tools/trs/trs_tool.hpp"

#include "erhe/toolkit/verify.hpp"

namespace editor
{

Rotate_tool* g_rotate_tool{nullptr};

Rotate_tool::Rotate_tool()
    : erhe::components::Component{c_type_name}
{
}

Rotate_tool::~Rotate_tool() noexcept
{
    ERHE_VERIFY(g_rotate_tool == this);
    g_rotate_tool = nullptr;
}

void Rotate_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Rotate_tool::initialize_component()
{
    ERHE_VERIFY(g_rotate_tool == nullptr);
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.rotate);
    g_tools->register_tool(this);
    g_rotate_tool = this;
}

void Rotate_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (g_trs_tool != nullptr)
    {
        g_trs_tool->set_rotate(new_priority > old_priority);
    }
}

} // namespace editor
