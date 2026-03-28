// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_rendergraph/rendergraph.hpp"
#include "erhe_rendergraph/rendergraph_node.hpp"
#include "erhe_rendergraph/rendergraph_log.hpp"
#include "erhe_graph/graph.hpp"
#include "erhe_graph/node.hpp"
#include "erhe_graph/pin.hpp"
#include "erhe_graph/link.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

namespace erhe::rendergraph {

Rendergraph::Rendergraph(erhe::graphics::Device& graphics_device)
    : m_graphics_device{graphics_device}
    , m_graph          {std::make_unique<erhe::graph::Graph>()}
{
    m_nodes.reserve(128);
}

Rendergraph::~Rendergraph() noexcept
{
    // Mark all nodes as unregistered so their destructors (which may run
    // later if held by shared_ptr elsewhere) don't try to access this
    // destroyed Rendergraph.
    for (Rendergraph_node* node : m_nodes) {
        node->m_is_registered = false;
    }
    m_nodes.clear();
}

auto Rendergraph::get_nodes() const -> const std::vector<Rendergraph_node*>&
{
    return m_nodes;
}

void Rendergraph::sort()
{
    if (m_is_sorted) {
        return;
    }

    m_graph->sort();

    if (!m_graph->m_is_sorted) {
        // Sort failed (cycle detected). Log detailed diagnostics.
        log_frame->error("Rendergraph sort failed - graph is not acyclic:");
        for (Rendergraph_node* node : m_nodes) {
            log_frame->info("    Node: {}", node->get_name());
            for (const erhe::graph::Pin& pin : node->get_input_pins()) {
                log_frame->info("        Input key: {}", pin.get_key());
                for (erhe::graph::Link* link : pin.get_links()) {
                    erhe::graph::Pin* source_pin = link->get_source();
                    if (source_pin != nullptr) {
                        Rendergraph_node* producer = static_cast<Rendergraph_node*>(source_pin->get_owner_node());
                        if (producer != nullptr) {
                            log_frame->info("          producer: {}", producer->get_name());
                        }
                    }
                }
            }
        }
        return;
    }

    // Map sorted graph nodes back to Rendergraph_node pointers
    m_nodes.clear();
    for (erhe::graph::Node* graph_node : m_graph->get_nodes()) {
        m_nodes.push_back(static_cast<Rendergraph_node*>(graph_node));
    }

    m_is_sorted = true;
}

void Rendergraph::execute()
{
    ERHE_PROFILE_FUNCTION();

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

    SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph with {} nodes:", m_nodes.size());

    sort();

    erhe::graphics::Scoped_debug_group render_graph_scope{
        erhe::utility::Debug_label{"Rendergraph::execute()"}
    };

    for (const auto& node : m_nodes) {
        if (node->is_enabled()) {
            SPDLOG_LOGGER_TRACE(log_frame, "Execute render graph node '{}'", node->get_name());
            erhe::graphics::Scoped_debug_group node_scope{node->get_debug_label()};
            node->execute_rendergraph_node();
        }
    }

    // Release deferred resources now that all nodes have completed execution.
    // Nodes may defer resource destruction during execute when old resources
    // are still referenced by nodes that execute later in the same frame.
    m_deferred_resources.clear();
}

void Rendergraph::defer_resource(std::shared_ptr<void> resource)
{
    m_deferred_resources.push_back(std::move(resource));
}

void Rendergraph::register_node(Rendergraph_node* node)
{
    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

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
    m_is_sorted = false;

    // Register directly with erhe::graph for topological sort
    m_graph->register_node(node);
    node->m_is_registered = true;

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

    std::lock_guard<ERHE_PROFILE_LOCKABLE_BASE(std::mutex)> lock{m_mutex};

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

    // Unregister from erhe::graph (this also disconnects all its links)
    m_graph->unregister_node(node);
    node->m_is_registered = false;

    m_nodes.erase(i);
    m_is_sorted = false;

    log_tail->trace("Unregistered Rendergraph_node {}", node->get_name());
}

auto Rendergraph::get_graphics_device() -> erhe::graphics::Device&
{
    return m_graphics_device;
}

void Rendergraph::automatic_layout(const float image_size)
{
    if (m_nodes.empty()) {
        return;
    }

    sort();

    // First, count how many nodes are at each column (== depth)
    std::vector<int> column_node_count(1);
    int max_column_node_count{0};
    int last_column{0};
    for (const auto& node : m_nodes) {
        const int column = node->get_depth();
        last_column = std::max(column, last_column);
        if (column_node_count.size() < static_cast<std::size_t>(column) + 1) {
            column_node_count.resize(column + 1);
        }
        ++column_node_count[column];
        max_column_node_count = std::max(max_column_node_count, column_node_count[column]);
    }

    // Figure out
    //  - total height for each column (== depth)
    //  - maximum column height
    std::vector<float> column_height;
    float max_column_height{0.0f};
    column_height.resize(column_node_count.size());
    for (const auto& node : m_nodes) {
        const int   column         = node->get_depth();
        const auto  node_size_opt  = node->get_size();
        const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
        const float aspect         = node_size.x / node_size.y;
        const auto  effective_size = glm::vec2{aspect * image_size, image_size};

        if (column_height[column] != 0.0f) {
            column_height[column] += y_gap;
        }
        column_height[column] += effective_size.y;
        max_column_height = std::max(column_height[column], max_column_height);
    }

    // Assign initial positions, not consider link crossings
    float x_offset = 0.0f;
    for (int column = 0; column <= last_column; ++column) {
        int   row_count    = column_node_count[column];
        float column_width = 0.0f;
        float y_offset     = 0.0f;

        std::vector<Rendergraph_node*> column_nodes;
        for (Rendergraph_node* node : m_nodes) {
            const int node_depth = node->get_depth();
            if (node_depth != column) {
                continue;
            }

            const auto  node_size_opt  = node->get_size();
            const auto  node_size      = node_size_opt.has_value() ? node_size_opt.value() : glm::vec2{400.0f, 100.0f};
            const float aspect         = node_size.x / node_size.y;
            const auto  effective_size = glm::vec2{aspect * image_size, image_size};

            column_width = std::max(column_width, effective_size.x);
            if (row_count == 1) {
                node->set_position(glm::vec2{x_offset, max_column_height * 0.5f - effective_size.y});
            } else {
                node->set_position(glm::vec2{x_offset, y_offset});
            }
            y_offset += effective_size.y + y_gap;
        }
        x_offset += column_width + x_gap;
    }

    // TODO: Sort column_nodes by average source Y position to reduce link crossings
}

auto Rendergraph::connect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    // Find output pin on source with matching key
    erhe::graph::Pin* source_pin = nullptr;
    for (erhe::graph::Pin& pin : source->get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            source_pin = &pin;
            break;
        }
    }
    if (source_pin == nullptr) {
        log_tail->error("Rendergraph::connect(): source '{}' has no output pin for key {}", source->get_name(), key);
        return false;
    }

    // Find input pin on sink with matching key
    erhe::graph::Pin* sink_pin = nullptr;
    for (erhe::graph::Pin& pin : sink->get_input_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            sink_pin = &pin;
            break;
        }
    }
    if (sink_pin == nullptr) {
        log_tail->error("Rendergraph::connect(): sink '{}' has no input pin for key {}", sink->get_name(), key);
        return false;
    }

    erhe::graph::Link* link = m_graph->connect(source_pin, sink_pin);
    if (link == nullptr) {
        return false;
    }

    sink->set_depth(source->get_depth() + 1);

    m_is_sorted = false;
    log_tail->trace("Rendergraph: Connected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());
    return true;
}

auto Rendergraph::disconnect(const int key, Rendergraph_node* source, Rendergraph_node* sink) -> bool
{
    ERHE_VERIFY(source != nullptr);
    ERHE_VERIFY(sink != nullptr);

    erhe::graph::Link* link_to_remove = nullptr;
    for (erhe::graph::Pin& pin : source->get_output_pins()) {
        if (pin.get_key() == static_cast<std::size_t>(key)) {
            for (erhe::graph::Link* link : pin.get_links()) {
                if (link->get_sink() != nullptr && link->get_sink()->get_owner_node() == sink) {
                    link_to_remove = link;
                    break;
                }
            }
            if (link_to_remove != nullptr) {
                break;
            }
        }
    }
    if (link_to_remove != nullptr) {
        m_graph->disconnect(link_to_remove);
    }

    m_is_sorted = false;
    log_tail->trace("Rendergraph: disconnected key: {} from: {} to: {}", key, source->get_name(), sink->get_name());

    return true;
}

} // namespace erhe::rendergraph
