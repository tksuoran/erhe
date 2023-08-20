#pragma once

#include "editor_message.hpp"

#include "erhe_message_bus/message_bus.hpp"

namespace editor
{

class Editor_message_bus
    : public erhe::message_bus::Message_bus<Editor_message>
{
public:
    Editor_message_bus();
};

} // namespace editor
