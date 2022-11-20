#pragma once

#include <functional>
#include <memory>
#include <queue>
#include <vector>

namespace erhe::scene
{

class Bus_node;
class Message;
class Message_bus;
class Node;

enum class Node_event_type : int
{
    node_added_to_scene,
    node_removed_from_scene,
    node_replaced,
    node_changed
};

class Message
{
public:
    Node_event_type       event_type;
    std::shared_ptr<Node> lhs;
    std::shared_ptr<Node> rhs;
};

class Message_bus_node
{
public:
    explicit Message_bus_node(Message_bus* message_bus);
    virtual ~Message_bus_node();

protected:
    Message_bus* m_message_bus;

    std::function<void (Message&)> get_notify_func();
    void                           send           (Message message);

    virtual void on_notify(Message& message);
};

class Message_bus
{
public:
    Message_bus();
    ~Message_bus();

    void add_receiver(std::function<void(Message&)> message_receiver);
    void send_message(Message message);
    void notify();

private:
    std::vector<std::function<void (Message&)>> m_receivers;
    std::queue<Message>                         m_messages;
};

}
