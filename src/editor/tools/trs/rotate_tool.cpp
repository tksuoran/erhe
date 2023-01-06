#include "tools/trs/rotate_tool.hpp"
#include "graphics//icon_set.hpp"
#include "tools/tools.hpp"
#include "tools/trs/trs_tool.hpp"

namespace editor
{

Rotate_tool::Rotate_tool()
    : erhe::components::Component{c_type_name}
{
}

Rotate_tool::~Rotate_tool() noexcept
{
}

void Rotate_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Rotate_tool::initialize_component()
{
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (get<Icon_set>()->icons.rotate);
    get<Tools>()->register_tool(this);
}

void Rotate_tool::post_initialize()
{
    m_trs_tool = try_get<Trs_tool>();
}

void Rotate_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (m_trs_tool)
    {
        m_trs_tool->set_rotate(new_priority > old_priority);
    }
}

} // namespace editor
