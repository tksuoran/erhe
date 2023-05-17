#include "tools/transform/scale_tool.hpp"

#include "graphics/icon_set.hpp"
#include "tools/tools.hpp"
#include "tools/transform/transform_tool.hpp"

#include "erhe/toolkit/verify.hpp"

namespace editor
{

Scale_tool* g_scale_tool{nullptr};

Scale_tool::Scale_tool()
    : erhe::components::Component{c_type_name}
{
}

Scale_tool::~Scale_tool() noexcept
{
    ERHE_VERIFY(g_scale_tool == nullptr);
}

void Scale_tool::deinitialize_component()
{
    ERHE_VERIFY(g_scale_tool == this);
    g_scale_tool = nullptr;
}

void Scale_tool::declare_required_components()
{
    require<Icon_set>();
    require<Tools   >();
}

void Scale_tool::initialize_component()
{
    ERHE_VERIFY(g_scale_tool == nullptr);
    set_base_priority(c_priority);
    set_description  (c_title);
    set_flags        (Tool_flags::toolbox | Tool_flags::allow_secondary);
    set_icon         (g_icon_set->icons.scale);
    g_tools->register_tool(this);
    g_scale_tool = this;
}

void Scale_tool::handle_priority_update(int old_priority, int new_priority)
{
    if (g_transform_tool != nullptr) {
        g_transform_tool->set_scale(new_priority > old_priority);
    }
}

} // namespace editor
