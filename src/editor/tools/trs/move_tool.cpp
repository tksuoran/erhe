#include "tools/trs/move_tool.hpp"
#include "graphics//icon_set.hpp"
#include "tools/tools.hpp"
#include "tools/trs/trs_tool.hpp"

namespace editor
{

Move_tool::Move_tool()
    : erhe::components::Component{c_type_name}
{
}

Move_tool::~Move_tool() noexcept
{
}

void Move_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Move_tool::initialize_component()
{
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (get<Icon_set>()->icons.move);
    get<Tools>()->register_tool(this);
}

void Move_tool::post_initialize()
{
    m_trs_tool = try_get<Trs_tool>();
}

void Move_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (m_trs_tool)
    {
        m_trs_tool->set_translate(new_priority > old_priority);
    }
}

} // namespace editor
