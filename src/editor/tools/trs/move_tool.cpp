#include "tools/trs/move_tool.hpp"

#include "graphics//icon_set.hpp"
#include "tools/tools.hpp"
#include "tools/trs/trs_tool.hpp"
#include "erhe/toolkit/verify.hpp"

namespace editor
{

Move_tool* g_move_tool{nullptr};

Move_tool::Move_tool()
    : erhe::components::Component{c_type_name}
{
}

Move_tool::~Move_tool() noexcept
{
    ERHE_VERIFY(g_move_tool == this);
    g_move_tool = nullptr;
}

void Move_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Move_tool::initialize_component()
{
    ERHE_VERIFY(g_move_tool == nullptr);

    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.move);
    g_tools->register_tool(this);

    g_move_tool = this;
}

void Move_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (g_trs_tool)
    {
        g_trs_tool->set_translate(new_priority > old_priority);
    }
}

} // namespace editor
