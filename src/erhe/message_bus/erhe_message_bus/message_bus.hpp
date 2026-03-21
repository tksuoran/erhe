#pragma once

#include "erhe_profile/profile.hpp"

#include <functional>
#include <mutex>
#include <queue>
#include <vector>

namespace erhe::message_bus {

//// TODO use something like https://github.com/TheWisp/signals instead
template <typename Message_type>
class Message_bus
{
public:
    void add_receiver(std::function<void(Message_type&)> message_receiver)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_queued_receivers_mutex};
        m_queued_receivers.push_back(message_receiver);
    }

    void send_message(Message_type message)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_receivers_mutex};
        for (auto iter = m_receivers.begin(); iter != m_receivers.end(); iter++) {
            (*iter)(message);
        }
    }

    void queue_message(Message_type message)
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_queued_messages_mutex};
        m_queued_messages.push(message);
    }

    void update()
    {
        std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_1{m_receivers_mutex};
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_2{m_messages_mutex};
            while (!m_messages.empty()) {
                for (auto iter = m_receivers.begin(); iter != m_receivers.end(); iter++) {
                    (*iter)(m_messages.front());
                }
                m_messages.pop();
            }
            {
                std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_3{m_queued_messages_mutex};
                m_messages = std::move(m_queued_messages);
            }
        }
        {
            std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock_4{m_queued_receivers_mutex};
            m_receivers.insert(m_receivers.end(), m_queued_receivers.begin(), m_queued_receivers.end());
            m_queued_receivers.clear();
        }
    }

private:
    ERHE_PROFILE_MUTEX(std::mutex,                   m_receivers_mutex);
    std::vector<std::function<void (Message_type&)>> m_receivers;

    ERHE_PROFILE_MUTEX(std::mutex,                   m_queued_receivers_mutex);
    std::vector<std::function<void (Message_type&)>> m_queued_receivers;

    ERHE_PROFILE_MUTEX(std::mutex,                   m_messages_mutex);
    std::queue<Message_type>                         m_messages;

    ERHE_PROFILE_MUTEX(std::mutex,                   m_queued_messages_mutex);
    std::queue<Message_type>                         m_queued_messages;

};

} // namespace erhe::message_bus
