#include "erhe_graph/graph.hpp"
#include "erhe_graph/graph_log.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"

#include <spdlog/spdlog.h>

namespace erhe::graph {

void Graph::register_node(Node* node)
{
#if !defined(NDEBUG)
    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Node* entry) { return entry == node; });
    if (i != m_nodes.end()) {
        log_graph->error("Node {} {} is already registered to Graph", node->get_name(), node->get_id());
        return;
    }
#endif
    m_nodes.push_back(node);
    log_graph->trace("Registered Node {} {}", node->get_name(), node->get_id());
}

void Graph::unregister_node(Node* node)
{
    if (node == nullptr) {
        return;
    }

    const auto i = std::find_if(m_nodes.begin(), m_nodes.end(), [node](Node* entry) { return entry == node; });
    if (i == m_nodes.end()) {
        log_graph->error("Graph::unregister_node(): Node {} {} is not registered", node->get_name(), node->get_id());
        return;
    }

    std::vector<Pin>& input_pins = node->get_input_pins();
    for (Pin& pin : input_pins) {
        for (Link* link : pin.get_links()) {
            disconnect(link);
        }
    }
    std::vector<Pin>& output_pins = node->get_output_pins();
    for (Pin& pin : output_pins) {
        for (Link* link : pin.get_links()) {
            disconnect(link);
        }
    }

    m_nodes.erase(i);

    log_graph->trace("Unregistered Node {} {}", node->get_name(), node->get_id());
}

auto Graph::connect(Pin* source_pin, Pin* sink_pin) -> Link*
{
    ERHE_VERIFY(source_pin != nullptr);
    ERHE_VERIFY(sink_pin != nullptr);

    if (sink_pin->get_key() != source_pin->get_key()) {
        log_graph->warn("Sink pin key {} does not match source pin key {}", sink_pin->get_key(), source_pin->get_key());
        return nullptr;
    }
    m_links.push_back(std::make_unique<Link>(source_pin, sink_pin));
    Link* link = m_links.back().get();
    sink_pin  ->add_link(link);
    source_pin->add_link(link);

    log_graph->trace("Connected {} {} to {} {} ", source_pin->get_name(), source_pin->get_id(), sink_pin->get_name(), sink_pin->get_id());
    return link;
}

void Graph::disconnect(Link* link)
{
    ERHE_VERIFY(link != nullptr);

    auto i = std::find_if(m_links.begin(), m_links.end(), [link](const std::unique_ptr<Link>& entry){ return link == entry.get(); });
    if (i == m_links.end()) {
        log_graph->error("Link not found");
        return;
    }
    link->disconnect();
    m_links.erase(i);
}

auto Graph::get_host_name() const -> const char*
{
    return "Graph";
}

void Graph::sort()
{
    if (m_is_sorted) {
        return;
    }

    std::vector<Node*> unsorted_nodes = m_nodes;
    std::vector<Node*> sorted_nodes;

    while (!unsorted_nodes.empty()) {
        bool found_node{false};
        for (const auto& node : unsorted_nodes) {
            {
                bool any_missing_dependency{false};
                for (const Pin& input : node->get_input_pins()) {
                    for (Link* link : input.get_links()) {
                        // See if dependency is in already sorted nodes
                        const auto i = std::find_if(
                            sorted_nodes.begin(),
                            sorted_nodes.end(),
                            [&link](Node* entry) {
                                return entry == link->get_source()->get_owner_node();
                            }
                        );
                        if (i == sorted_nodes.end()) {
                            any_missing_dependency = true;
                            break;
                        }
                    }
                }
                if (any_missing_dependency) {
                    continue;
                }
            }

            SPDLOG_LOGGER_TRACE(log_graph, "Sort: Selected node {} - all dependencies are met", node->get_name(), node->get_id());
            found_node = true;

            // Add selected node to sorted nodes
            sorted_nodes.push_back(node);

            // Remove from unsorted nodes
            const auto i = std::remove(unsorted_nodes.begin(), unsorted_nodes.end(), node);
            if (i == unsorted_nodes.end()) {
                log_graph->error("Sort: Node {} {} is not in graph nodes", node->get_name(), node->get_id());
            } else {
                unsorted_nodes.erase(i, unsorted_nodes.end());
            }

            // restart loop
            break;
        }

        if (!found_node) {
            log_graph->error("No node with met dependencies found. Graph is not acyclic:");
            return;
        }
    }

    std::swap(m_nodes, sorted_nodes);
    m_is_sorted = true;
}

auto Graph::get_nodes() const -> const std::vector<Node*>&
{
    return m_nodes;
}

auto Graph::get_nodes() -> std::vector<Node*>&
{
    return m_nodes;
}

auto Graph::get_links() -> std::vector<std::unique_ptr<Link>>&
{
    return m_links;
}

} // namespace erhe::graph
