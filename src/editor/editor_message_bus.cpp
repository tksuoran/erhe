#include "editor_message_bus.hpp"

//#include "editor_log.hpp"

namespace editor
{

Editor_message_bus::Editor_message_bus()
    : erhe::components::Component{c_type_name}
{
}

Editor_message_bus::~Editor_message_bus() noexcept = default;

//void Scene_message_bus::declare_required_components()
//{
//}
//
//void Scene_message_bus::initialize_component()
//{
//}
//
//void Scene_message_bus::post_initialize()
//{
//}

} // namespace editor
