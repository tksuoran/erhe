// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/render_graph.hpp"
#include "erhe/application/render_graph_node.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/graphics/debug.hpp"

namespace erhe::application {

Render_graph::Render_graph()
    : erhe::components::Component{c_label}
{
}

Render_graph::~Render_graph()
{
}

[[nodiscard]] auto Render_graph::get_nodes() const -> const std::vector<std::shared_ptr<Render_graph_node>>&
{
    return m_nodes;
}

void Render_graph::sort()
{
    //std::lock_guard<std::mutex> lock{m_mutex};

    std::vector<std::shared_ptr<Render_graph_node>> unsorted_nodes = m_nodes;
    std::vector<std::shared_ptr<Render_graph_node>> sorted_nodes;

    while (!unsorted_nodes.empty())
    {
        bool found_node{false};
        for (const auto& node : unsorted_nodes)
        {
            bool any_missing_dependency{false};
            SPDLOG_LOGGER_TRACE(
                log_render_graph,
                "Sort: Considering node '{}'", node->name()
            );
            for (const auto* dependency : node->get_dependencies())
            {
                // See if dependency is in already sorted nodes
                const auto i = std::find_if(
                    sorted_nodes.begin(),
                    sorted_nodes.end(),
                    [dependency](const auto& element)
                    {
                        return element.get() == dependency;
                    }
                );
                if (i == sorted_nodes.end())
                {
                    SPDLOG_LOGGER_TRACE(
                        log_render_graph,
                        "Sort: Considering node '{}' failed - has unmet dependency '{}'",
                        node->name(),
                        dependency->name()
                    );
                    any_missing_dependency = true;
                    break;
                }
            }
            if (any_missing_dependency)
            {
                continue;
            }

            SPDLOG_LOGGER_TRACE(
                log_render_graph,
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
                log_render_graph->error("Sort: Node '{}' is not in graph nodes", node->name());
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
            log_render_graph->error(
                "No render graph node with met dependencies found. Graph is not acyclic:"
            );
            for (const auto& node : m_nodes)
            {
                log_render_graph->info("    {}", node->name());
                for (const auto& dependency : node->get_dependencies())
                {
                    log_render_graph->info("        {}", dependency->name());
                }
            }

            log_render_graph->info("sorted nodes:");
            for (const auto& node : sorted_nodes)
            {
                log_render_graph->info("    {}", node->name());
            }

            log_render_graph->info("unsorted nodes:");
            for (const auto& node : unsorted_nodes)
            {
                log_render_graph->info("    {}", node->name());
            }
            return;
        }
    }

    std::swap(m_nodes, sorted_nodes);
}

void Render_graph::execute()
{
    std::lock_guard<std::mutex> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(log_render_graph, "Execute render graph");

    sort();

    static constexpr std::string_view c_render_graph{"Render graph"};
    erhe::graphics::Scoped_debug_group render_graph_scope{c_render_graph};

    for (const auto& node : m_nodes)
    {
        SPDLOG_LOGGER_TRACE(log_render_graph, "Execute render graph node '{}'", node->name());
        erhe::graphics::Scoped_debug_group render_graph_node_scope{node->name()};
        node->execute_render_graph_node();
    }
}

void Render_graph::register_node(const std::shared_ptr<Render_graph_node>& node)
{
    std::lock_guard<std::mutex> lock{m_mutex};

#if !defined(NDEBUG)
    const auto i = std::find(m_nodes.begin(), m_nodes.end(), node);
    if (i != m_nodes.end())
    {
        log_render_graph->warn("Node '{}' is already added to graph", node->name());
        return;
    }
#endif
    m_nodes.push_back(node);
}

} // namespace erhe::application
