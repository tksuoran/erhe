// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/graphics/debug.hpp"

namespace erhe::application {

Rendergraph::Rendergraph()
    : erhe::components::Component{c_type_name}
{
}

Rendergraph::~Rendergraph()
{
}

[[nodiscard]] auto Rendergraph::get_nodes() const -> const std::vector<std::shared_ptr<Rendergraph_node>>&
{
    return m_nodes;
}

void Rendergraph::sort()
{
    //std::lock_guard<std::mutex> lock{m_mutex};

    std::vector<std::shared_ptr<Rendergraph_node>> unsorted_nodes = m_nodes;
    std::vector<std::shared_ptr<Rendergraph_node>> sorted_nodes;

    while (!unsorted_nodes.empty())
    {
        bool found_node{false};
        for (const auto& node : unsorted_nodes)
        {
            SPDLOG_LOGGER_TRACE(
                log_rendergraph,
                "Sort: Considering node '{}'", node->name()
            );
            {
                bool any_missing_dependency{false};
                for (const Rendergraph_consumer_connector& input : node->get_inputs())
                {
                    for (const auto& producer_node : input.producer_nodes)
                    {
                        // See if dependency is in already sorted nodes
                        const auto i = std::find_if(
                            sorted_nodes.begin(),
                            sorted_nodes.end(),
                            [&producer_node](const std::shared_ptr<Rendergraph_node>& entry)
                            {
                                return entry == producer_node.lock();
                            }
                        );
                        if (i == sorted_nodes.end())
                        {
                            SPDLOG_LOGGER_TRACE(
                                log_rendergraph,
                                "Sort: Considering node '{}' failed - has unmet dependency key '{}' node '{}'",
                                node->name(),
                                input.key,
                                other_node->name()
                            );
                            any_missing_dependency = true;
                            break;
                        }
                    }
                }
                if (any_missing_dependency)
                {
                    continue;
                }
            }

            SPDLOG_LOGGER_TRACE(
                log_rendergraph,
                "Sort: Selected node '{}' - all dependencies are met",
                node->name()
            );
            found_node = true;

            // Add selected node to sorted nodes
            sorted_nodes.push_back(node);

            // Remove from unsorted nodes
            const auto i = std::remove(unsorted_nodes.begin(), unsorted_nodes.end(), node);
            if (i == unsorted_nodes.end())
            {
                log_rendergraph->error("Sort: Node '{}' is not in graph nodes", node->name());
            }
            else
            {
                unsorted_nodes.erase(i, unsorted_nodes.end());
            }

            // restart loop
            break;
        }

        if (!found_node)
        {
            log_rendergraph->error(
                "No render graph node with met dependencies found. Graph is not acyclic:"
            );
            for (const auto& node : m_nodes)
            {
                log_rendergraph->info("    Node: {}", node->name());
                for (const Rendergraph_consumer_connector& input : node->get_inputs())
                {
                    log_rendergraph->info("        Input key: {}", input.key);
                    for (const auto& producer_node : input.producer_nodes)
                    {
                        const auto& producer = producer_node.lock();
                        if (!producer)
                        {
                            continue;
                        }
                        log_rendergraph->info("          producer: {}", producer->name());
                    }
                }
            }

            log_rendergraph->info("sorted nodes:");
            for (const auto& node : sorted_nodes)
            {
                log_rendergraph->info("    Node: {}", node->name());
            }

            log_rendergraph->info("unsorted nodes:");
            for (const auto& node : unsorted_nodes)
            {
                log_rendergraph->info("    Node: {}", node->name());
                for (const auto& input : node->get_inputs())
                {
                    log_rendergraph->info("        Input key: {}", input.key);
                    for (const auto& producer_node : input.producer_nodes)
                    {
                        const auto& producer = producer_node.lock();
                        if (!producer)
                        {
                            continue;
                        }
                        log_rendergraph->info("          producer: {}", producer->name());
                    }
                }
            }
            return;
        }
    }

    std::swap(m_nodes, sorted_nodes);
}

void Rendergraph::execute()
{
    std::lock_guard<std::mutex> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(log_rendergraph, "Execute render graph");

    sort();

    static constexpr std::string_view c_render_graph{"Render graph"};
    erhe::graphics::Scoped_debug_group render_graph_scope{c_render_graph};

    for (const auto& node : m_nodes)
    {
        SPDLOG_LOGGER_TRACE(log_rendergraph, "Execute render graph node '{}'", node->name());
        erhe::graphics::Scoped_debug_group render_graph_node_scope{node->name()};
        node->execute_rendergraph_node();
    }
}

void Rendergraph::register_node(const std::shared_ptr<Rendergraph_node>& node)
{
    std::lock_guard<std::mutex> lock{m_mutex};

    const auto* node_raw = node.get();
#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node_raw](
            const std::shared_ptr<Rendergraph_node>& entry
        )
        {
            return entry.get() == node_raw;
        }
    );
    if (i != m_nodes.end())
    {
        log_rendergraph->warn("Rendergraph_node '{}' is already registered to Rendergraph", node->name());
        return;
    }
#endif
    m_nodes.push_back(node);
    float x = static_cast<float>(m_nodes.size()) * 250.0f;
    float y = 0.0f;
    node->set_position(glm::vec2{x, y});

    log_rendergraph->info("Registered Rendergraph_node {}", node->name());
}

void Rendergraph::automatic_layout()
{
    std::vector<int> node_count_per_depth(1);
    for (const auto& node : m_nodes)
    {
        const int depth = node->get_depth();
        if (node_count_per_depth.size() < depth + 1)
        {
            node_count_per_depth.resize(depth + 1);
        }
        ++node_count_per_depth[depth];
    }

    constexpr float layout_width  = 400.0f;
    constexpr float layout_height = 160.0f;

    for (const auto& node : m_nodes)
    {
        const int depth = node->get_depth();
        const int row = node_count_per_depth[depth]--;
        node->set_position(
            glm::vec2{
                static_cast<float>(depth) * layout_width,
                static_cast<float>(row) * layout_height
            }
        );
    }
}

auto Rendergraph::connect(
    const int                       key,
    std::weak_ptr<Rendergraph_node> source_node,
    std::weak_ptr<Rendergraph_node> sink_node
) -> bool
{
    const auto& source = source_node.lock();
    if (!source)
    {
        log_rendergraph->error("Rendergraph connection '{}' source node is expired", key);
        return false;
    }

    const auto& sink = sink_node.lock();
    if (!sink)
    {
        log_rendergraph->error("Rendergraph connection '{}' sink node is expired", key);
        return false;
    }

    const bool sink_connected = sink->connect_input(key, source_node);
    if (!sink_connected)
    {
        return false;
    }
    const bool source_connected = source->connect_output(key, sink_node);
    if (!source_connected)
    {
        return false;
    }

    log_rendergraph->info("Rendergraph: Connected key: {} from: {} to: {}", key, source->name(), sink->name());
    automatic_layout();
    return true;
}

} // namespace erhe::application
