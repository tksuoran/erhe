#pragma once

#include "erhe_profile/profile.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::graph {
    class Graph;
}

namespace erhe::graphics {
    class Device;
}

namespace erhe::rendergraph {

class Rendergraph_node;

class Rendergraph final
{
public:
    explicit Rendergraph(erhe::graphics::Device& graphics_device);
    ~Rendergraph() noexcept;

    // Public API
    [[nodiscard]] auto get_nodes() const -> const std::vector<Rendergraph_node*>&;
    void sort           ();
    void execute        ();
    void register_node  (Rendergraph_node* node);
    void unregister_node(Rendergraph_node* node);
    auto connect        (int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;
    auto disconnect     (int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;

    [[nodiscard]] auto get_graphics_device() -> erhe::graphics::Device&;

    void automatic_layout(float image_size);

    // Defer resource destruction until after execute() completes.
    // Use this when a node replaces resources during execution that
    // may still be referenced by nodes executing later in the same frame.
    void defer_resource(std::shared_ptr<void> resource);

    float x_gap{100.0f};
    float y_gap{100.0f};

private:
    erhe::graphics::Device&        m_graphics_device;
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Rendergraph_node*> m_nodes;
    bool                           m_is_sorted{false};

    // Internal erhe::graph for topological sort
    std::unique_ptr<erhe::graph::Graph> m_graph;

    // Resources deferred for destruction until after execute() completes
    std::vector<std::shared_ptr<void>> m_deferred_resources;
};

} // namespace erhe::rendergraph
