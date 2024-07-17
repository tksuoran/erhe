// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graphics/debug.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

Rendergraph::Rendergraph(erhe::graphics::Instance& graphics_instance)
    : m_graphics_instance{graphics_instance}
{
    log_tail->info("Rendergraph::Rendergraph()");
}

Rendergraph::~Rendergraph()
{
    log_tail->info("Rendergraph::~Rendergraph()");
}

auto Rendergraph::get_nodes() const -> const std::vector<Rendergraph_node*>&
{
    return m_nodes;
}

void Rendergraph::sort()
{
    //std::lock_guard<std::mutex> lock{m_mutex};

    std::vector<Rendergraph_node*> unsorted_nodes = m_nodes;
    std::vector<Rendergraph_node*> sorted_nodes;

    while (!unsorted_nodes.empty()) {
        bool found_node{false};
        for (const auto& node : unsorted_nodes) {
            SPDLOG_LOGGER_TRACE(log_frame, "Sort: Considering node '{}'", node->get_name());
            {
                bool any_missing_dependency{false};
                for (const Rendergraph_consumer_connector& input : node->get_inputs()) {
                    for (auto* producer_node : input.producer_nodes) {
                        // See if dependency is in already sorted nodes
                        const auto i = std::find_if(
                            sorted_nodes.begin(),
                            sorted_nodes.end(),
                            [&producer_node](Rendergraph_node* entry) {
                                return entry == producer_node;
                            }
                        );
                        if (i == sorted_nodes.end()) {
                            //// const auto& producer = producer_node.lock();
                            SPDLOG_LOGGER_TRACE(
                                log_frame,
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
                if (any_missing_dependency) {
                    continue;
                }
            }

            SPDLOG_LOGGER_TRACE(log_frame, "Sort: Selected node '{}' - all dependencies are met", node->get_name());
            found_node = true;

            // Add selected node to sorted nodes
            sorted_nodes.push_back(node);

            // Remove from unsorted nodes
            const auto i = std::remove(unsorted_nodes.begin(), unsorted_nodes.end(), node);
            if (i == unsorted_nodes.end()) {
                log_frame->error("Sort: Node '{}' is not in graph nodes", node->get_name());
            } else {
                unsorted_nodes.erase(i, unsorted_nodes.end());
            }

            // restart loop
            break;
        }

        if (!found_node) {
            log_frame->error("No render graph node with met dependencies found. Graph is not acyclic:");
            for (auto* node : m_nodes) {
                log_frame->info("    Node: {}", node->get_name());
                for (const Rendergraph_consumer_connector& input : node->get_inputs()) {
                    log_frame->info("        Input key: {}", input.key);
                    for (auto* producer_node : input.producer_nodes) {
                        log_frame->info("          producer: {}", producer_node->get_name());
                    }
                }
            }

            log_frame->info("sorted nodes:");
            for (auto* node : sorted_nodes) {
                log_frame->info("    Node: {}", node->get_name());
            }

            log_frame->info("unsorted nodes:");
            for (auto* node : unsorted_nodes) {
                log_frame->info("    Node: {}", node->get_name());
                for (const auto& input : node->get_inputs()) {
                    log_frame->info("        Input key: {}", input.key);
                    for (auto* producer_node : input.producer_nodes) {
                        log_frame->info("          producer: {}", producer_node->get_name());
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
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<std::mutex> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph with {} nodes:", m_nodes.size());

    sort();

    static constexpr std::string_view c_render_graph{"Render graph"};
    erhe::graphics::Scoped_debug_group render_graph_scope{c_render_graph};

    for (const auto& node : m_nodes) {
        if (node->is_enabled()) {
            SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph node '{}'", node->get_name());
            erhe::graphics::Scoped_debug_group render_graph_node_scope{node->get_name()};
            node->execute_rendergraph_node();
        }
    }
}

void Rendergraph::register_node(Rendergraph_node* node)
{
    std::lock_guard<std::mutex> lock{m_mutex};

#if !defined(NDEBUG)
    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node](Rendergraph_node* entry) {
            return entry == node;
        }
    );
    if (i != m_nodes.end()) {
        log_tail->error("Rendergraph_node '{}' is already registered to Rendergraph", node->get_name());
        return;
    }
#endif
    m_nodes.push_back(node);
    float x = static_cast<float>(m_nodes.size()) * 250.0f;
    float y = 0.0f;
    node->set_position(glm::vec2{x, y});

    log_tail->trace("Registered Rendergraph_node {}", node->get_name());
}

void Rendergraph::unregister_node(Rendergraph_node* node)
{
    if (node == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock{m_mutex};

    const auto i = std::find_if(
        m_nodes.begin(),
        m_nodes.end(),
        [node](Rendergraph_node* entry) {
            return entry == node;
        }
    );
    if (i == m_nodes.end()) {
        log_tail->error("Rendergraph::unregister_node(): node '{}' is not registered", node->get_name());
        return;
    }

    auto& inputs = node->get_inputs();
    for (auto& input : inputs) {
        for (auto* producer_node : input.producer_nodes) {
            disconnect(input.key, producer_node, node);
        }
        input.producer_nodes.clear();
    }
    auto& outputs = node->get_outputs();
    for (auto& output : outputs) {
        for (auto* consumer_node : output.consumer_nodes) {
            disconnect(output.key, node, consumer_node);
        }
        output.consumer_nodes.clear();
    }

    m_nodes.erase(i);

    log_tail->trace("Unregistered Rendergraph_node {}", node->get_name());
}

auto Rendergraph::get_graphics_instance() -> erhe::graphics::Instance&
{
    return m_graphics_instance;
}

void Rendergraph::automatic_layout(const float image_size)
{
    if (m_nodes.empty()) {
        return;
    }

    // First, count how many nodes are at each depth (== column)
    std::vector<int> node_count_per_depth(1);
    int max_depth{0};
    for (const auto& node : m_nodes) {
        const int depth = node->get_depth();
        max_depth = std::max(depth, max_depth);
        if (node_count_per_depth.size() < static_cast<std::size_t>(depth) + 1) {
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
    for (const auto& node : m_nodes) {
        const int   depth          = node->get_depth();
        const auto  node_size_opt  = node->get_size();
        const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
        const float aspect         = node_size.x / node_size.y;
        const auto  effective_size = glm::vec2{aspect * image_size, image_size};

        if (total_height_per_depth[depth] != 0.0f) {
            total_height_per_depth[depth] += y_gap;
        }
        total_height_per_depth[depth] += effective_size.y;
        max_total_height = std::max(total_height_per_depth[depth], max_total_height);
    }

    float x_offset = 0.0f;
    for (int depth = 0; depth <= max_depth; ++depth) {
        int   row_count    = node_count_per_depth[depth];
        float column_width = 0.0f;
        float y_offset     = 0.0f;
        for (
            auto i = m_nodes.begin(), end = m_nodes.end();
            i != end;
            ++i
        ) {
            const auto& node           = *i;
            const auto  node_size_opt  = node->get_size();
            const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
            const float aspect         = node_size.x / node_size.y;
            const auto  effective_size = glm::vec2{aspect * image_size, image_size};
            const int   node_depth     = node->get_depth();
            if (node_depth != depth) {
                continue;
            }
            column_width = std::max(column_width, effective_size.x);
            if (row_count == 1) {
                node->set_position(glm::vec2{x_offset, max_total_height * 0.5f - effective_size.y});
            } else {
                node->set_position(glm::vec2{x_offset, y_offset});
            }
            y_offset += effective_size.y + y_gap;
        }
        x_offset += column_width + x_gap;
    }
}

auto Rendergraph::connect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    const bool sink_connected = sink->connect_input(key, source);
    if (!sink_connected) {
        return false;
    }
    const bool source_connected = source->connect_output(key, sink);
    if (!source_connected) {
        return false;
    }

    log_tail->trace("Rendergraph: Connected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());
    //// automatic_layout();
    return true;
}

auto Rendergraph::disconnect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    /*const bool sink_disconnected   =*/ sink  ->disconnect_input(key, source);
    /*const bool source_disconnected =*/ source->disconnect_output(key, sink);

    log_tail->trace("Rendergraph: disconnected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());

    return true;
}

} // namespace erhe::rendergraph
