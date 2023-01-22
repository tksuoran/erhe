#include "editor_message_bus.hpp"

#include "erhe/toolkit/verify.hpp"

namespace editor
{

Editor_message_bus* g_editor_message_bus{nullptr};

Editor_message_bus::Editor_message_bus()
    : erhe::components::Component{c_type_name}
{

}

Editor_message_bus::~Editor_message_bus() noexcept
{
    ERHE_VERIFY(g_editor_message_bus == this);
    g_editor_message_bus = nullptr;
}

void Editor_message_bus::initialize_component()
{
    ERHE_VERIFY(g_editor_message_bus == nullptr);
    g_editor_message_bus = this;
}

} // namespace editor
