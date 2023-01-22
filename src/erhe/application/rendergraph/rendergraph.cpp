// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/application/rendergraph/rendergraph.hpp"
#include "erhe/application/rendergraph/rendergraph_node.hpp"
#include "erhe/application/application_log.hpp"

#include "erhe/graphics/debug.hpp"

namespace erhe::application {

Rendergraph* g_rendergraph{nullptr};

Rendergraph::Rendergraph()
    : erhe::components::Component{c_type_name}
{
}

Rendergraph::~Rendergraph()
{
    ERHE_VERIFY(g_rendergraph == nullptr);
}

void Rendergraph::deinitialize_component()
{
    ERHE_VERIFY(g_rendergraph == this);
    g_rendergraph = nullptr;
    m_nodes.clear();
}

void Rendergraph::initialize_component()
{
    ERHE_VERIFY(g_rendergraph == nullptr);
    g_rendergraph = this;
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
                "Sort: Considering node '{}'", node->get_name()
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
                            //// const auto& producer = producer_node.lock();
                            SPDLOG_LOGGER_TRACE(
                                log_rendergraph,
                                "Sort: Considering node '{}' failed - has unmet dependency key '{}' node '{}'",
                                node->get_name(),
                                input.key,
                                producer ? producer->get_name() : std::string{"(empty)"}
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
                node->get_name()
            );
            found_node = true;

            // Add selected node to sorted nodes
            sorted_nodes.push_back(node);

            // Remove from unsorted nodes
            const auto i = std::remove(unsorted_nodes.begin(), unsorted_nodes.end(), node);
            if (i == unsorted_nodes.end())
            {
                log_rendergraph->error("Sort: Node '{}' is not in graph nodes", node->get_name());
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
                log_rendergraph->info("    Node: {}", node->get_name());
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
                        log_rendergraph->info("          producer: {}", producer->get_name());
                    }
                }
            }

            log_rendergraph->info("sorted nodes:");
            for (const auto& node : sorted_nodes)
            {
                log_rendergraph->info("    Node: {}", node->get_name());
            }

            log_rendergraph->info("unsorted nodes:");
            for (const auto& node : unsorted_nodes)
            {
                log_rendergraph->info("    Node: {}", node->get_name());
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
                        log_rendergraph->info("          producer: {}", producer->get_name());
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

    SPDLOG_LOGGER_TRACE(log_rendergraph, "Execute render graph with {} nodes:", m_nodes.size());

    sort();

    static constexpr std::string_view c_render_graph{"Render graph"};
    erhe::graphics::Scoped_debug_group render_graph_scope{c_render_graph};

    for (const auto& node : m_nodes)
    {
        if (node->is_enabled())
        {
            SPDLOG_LOGGER_TRACE(log_rendergraph, "Execute render graph node '{}'", node->get_name());
            erhe::graphics::Scoped_debug_group render_graph_node_scope{node->get_name()};
            node->execute_rendergraph_node();
        }
    }
}

void Rendergraph::register_node(const std::shared_ptr<Rendergraph_node>& node)
{
    std::lock_guard<std::mutex> lock{m_mutex};

#if !defined(NDEBUG)
    const auto* node_raw = node.get();
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
        log_rendergraph->warn("Rendergraph_node '{}' is already registered to Rendergraph", node->get_name());
        return;
    }
#endif
    m_nodes.push_back(node);
    float x = static_cast<float>(m_nodes.size()) * 250.0f;
    float y = 0.0f;
    node->set_position(glm::vec2{x, y});

    log_rendergraph->info("Registered Rendergraph_node {}", node->get_name());
}

void Rendergraph::unregister_node(Rendergraph_node* node)
{
    if (node == nullptr)
    {
        return;
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node](
            const std::shared_ptr<Rendergraph_node>& entry
        )
        {
            return entry.get() == node;
        }
    );
    if (i == m_nodes.end())
    {
        log_rendergraph->error("Rendergraph::unregister_node(): node '{}' is not registered", node->get_name());
    }

    std::shared_ptr<Rendergraph_node> node_shared = *i;
    auto& inputs = node->get_inputs();
    for (auto& input : inputs)
    {
        for (const auto& producer_node : input.producer_nodes)
        {
            if (producer_node.expired())
            {
                continue;
            }
            disconnect(input.key, producer_node, node_shared);
        }
        input.producer_nodes.clear();
    }
    auto& outputs = node->get_outputs();
    for (auto& output : outputs)
    {
        for (const auto& consumer_node : output.consumer_nodes)
        {
            if (consumer_node.expired())
            {
                continue;
            }
            disconnect(output.key, node_shared, consumer_node);
        }
        output.consumer_nodes.clear();
    }

    m_nodes.erase(i);

    log_rendergraph->info("Unregistered Rendergraph_node {}", node->get_name());
}

void Rendergraph::automatic_layout(const float image_size)
{
    if (m_nodes.empty())
    {
        return;
    }

    // First, count how many nodes are at each depth (== column)
    std::vector<int> node_count_per_depth(1);
    int max_depth{0};
    for (const auto& node : m_nodes)
    {
        const int depth = node->get_depth();
        max_depth = std::max(depth, max_depth);
        if (node_count_per_depth.size() < static_cast<std::size_t>(depth) + 1)
        {
            node_count_per_depth.resize(depth + 1);
        }
        ++node_count_per_depth[depth];
    }

    // Figure out
    //  - total height for each depth (== column)
    //  - maximum column height
    std::vector<float> total_height_per_depth;
    float max_total_height{0.0f};
    total_height_per_depth.resize(node_count_per_depth.size());
    for (const auto& node : m_nodes)
    {
        const int   depth          = node->get_depth();
        const auto  node_size_opt  = node->get_size();
        const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
        const float aspect         = node_size.x / node_size.y;
        const auto  effective_size = glm::vec2{aspect * image_size, image_size};

        if (total_height_per_depth[depth] != 0.0f)
        {
            total_height_per_depth[depth] += y_gap;
        }
        total_height_per_depth[depth] += effective_size.y;
        max_total_height = std::max(total_height_per_depth[depth], max_total_height);
    }

    float x_offset = 0.0f;
    for (int depth = 0; depth <= max_depth; ++depth)
    {
        int   row_count    = node_count_per_depth[depth];
        float column_width = 0.0f;
        float y_offset     = 0.0f;
        for (
            auto i = m_nodes.begin(), end = m_nodes.end();
            i != end;
            ++i
        )
        {
            const auto& node           = *i;
            const auto  node_size_opt  = node->get_size();
            const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
            const float aspect         = node_size.x / node_size.y;
            const auto  effective_size = glm::vec2{aspect * image_size, image_size};
            const int   node_depth     = node->get_depth();
            if (node_depth != depth)
            {
                continue;
            }
            column_width = std::max(column_width, effective_size.x);
            if (row_count == 1)
            {
                node->set_position(glm::vec2{x_offset, max_total_height * 0.5f - effective_size.y});
            }
            else
            {
                node->set_position(glm::vec2{x_offset, y_offset});
            }
            y_offset += effective_size.y + y_gap;
        }
        x_offset += column_width + x_gap;
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

    log_rendergraph->info("Rendergraph: Connected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());
    //// automatic_layout();
    return true;
}

auto Rendergraph::disconnect(
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

    /*const bool sink_disconnected   =*/ sink  ->disconnect_input(key, source_node);
    /*const bool source_disconnected =*/ source->disconnect_output(key, sink_node);

    log_rendergraph->info("Rendergraph: disconnected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());

    return true;
}

} // namespace erhe::application
