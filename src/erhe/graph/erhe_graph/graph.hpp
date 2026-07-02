#pragma once

#include "erhe_item/item.hpp"
#include "erhe_item/item_host.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace erhe::graph {

class Node;
class Pin;
class Link;
class Graph;

[[nodiscard]] inline auto make_graph_id() -> int
{
    static std::atomic<int> counter{1};
    return counter++;
}

class Graph : public erhe::Item_host
{
public:
    // Implements Item_host
    auto get_host_name() const -> const char* override;

    void register_node  (Node* node);
    void unregister_node(Node* node);
    auto connect        (Pin* source_pin, Pin* sink_pin) -> Link*;
    void disconnect     (Link* link);
    void sort           ();

    // True when linking source_pin to sink_pin would make the graph cyclic,
    // i.e. the source pin's node is already reachable from the sink pin's
    // node by following existing links downstream (a pin connected to its
    // own node included). connect() refuses such links; UI link gestures
    // can pre-query this to reject them visually before the drop.
    [[nodiscard]] auto would_create_cycle(const Pin* source_pin, const Pin* sink_pin) const -> bool;

    [[nodiscard]] auto get_nodes() const -> const std::vector<Node*>&;
    [[nodiscard]] auto get_nodes()       -> std::vector<Node*>&;
    [[nodiscard]] auto get_links()       -> std::vector<std::unique_ptr<Link>>&;

    std::vector<Node*>                 m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;
    bool                               m_is_sorted{false};
};

} // namespace erhe::graph
