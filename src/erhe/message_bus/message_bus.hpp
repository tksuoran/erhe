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
    Message_bus() {}
    ~Message_bus() { }

    void add_receiver(std::function<void(Message_type&)> message_receiver) {
        m_receivers.push_back(message_receiver);
    }

    void send_message(Message_type message) {
        m_messages.push(message);
    }

    void notify() {
        while (!m_messages.empty()) {
            for (auto iter = m_receivers.begin(); iter != m_receivers.end(); iter++) {
                (*iter)(m_messages.front());
            }
            m_messages.pop();
        }
    }

private:
    std::vector<std::function<void (Message_type&)>> m_receivers;
    std::queue<Message_type>                         m_messages;
};

template <typename Message_type> class Message_bus_node
{
public:
    Message_bus_node() {}
    virtual ~Message_bus_node() {}

    void initialize(Message_bus<Message_type>* message_bus) {
        m_message_bus = message_bus;
        m_message_bus->add_receiver(get_notify_func());
    }

protected:
    Message_bus<Message_type>* m_message_bus{nullptr};

    auto get_notify_func() -> std::function<void (Message_type&)> {
        auto message_listener = [=](Message_type& message) -> void {
            this->on_notify(message);
        };
        return message_listener;
    }

    void send(Message_type message) const {
        m_message_bus->send_message(message);
    }

    virtual void on_notify(Message_type& message) {
        static_cast<void>(message);
    }
};

}
