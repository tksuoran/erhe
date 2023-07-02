#pragma once

#include "erhe/message_bus/message_bus.hpp"
#include "erhe/scene/scene_message.hpp"

namespace erhe::scene
{

class Scene_message_bus
    : public erhe::message_bus::Message_bus<erhe::scene::Scene_message>
{
public:
    Scene_message_bus();
};

} // namespace erhe::scene
