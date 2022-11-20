#include "erhe/scene/message_bus.hpp"

namespace erhe::scene
{

Message_bus_node::Message_bus_node(Message_bus* message_bus)
    : m_message_bus{message_bus}
{
    m_message_bus->add_receiver(get_notify_func());
}

Message_bus_node::~Message_bus_node() = default;

std::function<void (Message&)> Message_bus_node::get_notify_func()
{
    auto message_listener = [=](Message& message) -> void
    {
        this->on_notify(message);
    };
    return message_listener;
}

void Message_bus_node::send(Message message)
{
    m_message_bus->send_message(message);
}

void Message_bus_node::on_notify(Message& message)
{
    static_cast<void>(message);
}

//
//
//

Message_bus::Message_bus()
{
}

Message_bus::~Message_bus()
{
}

void Message_bus::add_receiver(
    std::function<void (Message&)> message_receiver
)
{
    m_receivers.push_back(message_receiver);
}

void Message_bus::send_message(Message message)
{
    m_messages.push(message);
}

void Message_bus::notify()
{
    while (!m_messages.empty())
    {
        for (
            auto iter = m_receivers.begin();
            iter != m_receivers.end();
            iter++
        )
        {
            (*iter)(m_messages.front());
        }

        m_messages.pop();
    }
}

} // namespace erhe::scene

