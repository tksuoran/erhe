#include "erhe/scene/scene_message_bus.hpp"
#include "erhe/toolkit/verify.hpp"

namespace erhe::scene
{

Scene_message_bus* g_scene_message_bus{nullptr};

Scene_message_bus::Scene_message_bus()
    : erhe::components::Component{c_type_name}
{
}

Scene_message_bus::~Scene_message_bus() noexcept
{
    ERHE_VERIFY(g_scene_message_bus == this);
    g_scene_message_bus = nullptr;
}

void Scene_message_bus::initialize_component()
{
    ERHE_VERIFY(g_scene_message_bus == nullptr);
    g_scene_message_bus = this;
}

} // namespace erhe::application
