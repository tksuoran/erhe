#pragma once

#include "erhe/application/rendergraph/resource_routing.hpp"
#include "erhe/components/components.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::application {

class Rendergraph_node;

/// <summary>
/// Rendergraph component manages all rendergraph nodes
/// </summary>
/// All Rendergraph_node instances must be registered to Rendergraph.
/// Connections between Rendergraph_node instances must be made via
/// Rendergraph.
class Rendergraph
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_type_name{"Render graph"};
    static constexpr uint32_t c_type_hash{
        compiletime_xxhash::xxh32(
            c_type_name.data(),
            c_type_name.size(),
            {}
        )
    };

    Rendergraph();
    ~Rendergraph();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return c_type_hash; }

    // Public API
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Rendergraph_node>>&;
    void sort           ();
    void execute        ();
    void register_node  (const std::shared_ptr<Rendergraph_node>& node);
    void unregister_node(Rendergraph_node* node);

    auto connect(
        int                             key,
        std::weak_ptr<Rendergraph_node> source_node,
        std::weak_ptr<Rendergraph_node> sink_node
    ) -> bool;

    auto disconnect(
        int                             key,
        std::weak_ptr<Rendergraph_node> source_node,
        std::weak_ptr<Rendergraph_node> sink_node
    ) -> bool;

    void automatic_layout();

private:
    std::mutex                                     m_mutex;
    std::vector<std::shared_ptr<Rendergraph_node>> m_nodes;
};

} // namespace erhe::application
