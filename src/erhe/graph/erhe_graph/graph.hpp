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

    [[nodiscard]] auto get_nodes() const -> const std::vector<Node*>&;
    [[nodiscard]] auto get_nodes()       -> std::vector<Node*>&;
    [[nodiscard]] auto get_links()       -> std::vector<std::unique_ptr<Link>>&;

    std::vector<Node*>                 m_nodes;
    std::vector<std::unique_ptr<Link>> m_links;
    bool                               m_is_sorted{false};
};

} // namespace erhe::graph
