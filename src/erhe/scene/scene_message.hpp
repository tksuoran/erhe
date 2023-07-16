#pragma once

#include <memory>

namespace erhe::scene
{

class Node;
class Scene;

enum class Scene_event_type : int
{
    invalid,
    node_added_to_scene,
    node_removed_from_scene,
    node_replaced,
    node_changed,
    selection_changed
};

class Scene_message
{
public:
    Scene_event_type      event_type{Scene_event_type::invalid};
    Scene*                scene{nullptr};
    std::shared_ptr<Node> lhs{};
    std::shared_ptr<Node> rhs{};
};

}
