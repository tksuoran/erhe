#pragma once

#include "app_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

namespace editor {

class App_message_bus : public erhe::message_bus::Message_bus<App_message>
{
public:
    App_message_bus();
};

} // namespace editor
