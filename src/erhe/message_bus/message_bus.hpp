#pragma once

#include <functional>
#include <memory>
#include <queue>
#include <vector>

namespace erhe::message_bus
{

template <typename Message_type>
class Message_bus
{
public:
    Message_bus()
    {
    }

    ~Message_bus()
    {
    }

    void add_receiver(std::function<void(Message_type&)> message_receiver)
    {
        m_receivers.push_back(message_receiver);
    }

    void send_message(Message_type message)
    {
        for (auto iter = m_receivers.begin(); iter != m_receivers.end(); iter++) {
            (*iter)(message);
        }
    }

    void queue_message(Message_type message)
    {
        m_messages.push(message);
    }

    void update()
    {
        while (!m_messages.empty())
        {
            for (auto iter = m_receivers.begin(); iter != m_receivers.end(); iter++)
            {
                (*iter)(m_messages.front());
            }
            m_messages.pop();
        }
    }

private:
    std::vector<std::function<void (Message_type&)>> m_receivers;
    std::queue<Message_type>                         m_messages;
};

}
