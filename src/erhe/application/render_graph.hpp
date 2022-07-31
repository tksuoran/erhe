#pragma once

#include "erhe/components/components.hpp"

#include <memory>
#include <mutex>
#include <vector>

namespace erhe::application {

class Render_graph_node;

class Render_graph
    : public erhe::components::Component
{
public:
    static constexpr std::string_view c_label{"Render graph"};
    static constexpr uint32_t hash{
        compiletime_xxhash::xxh32(
            c_label.data(),
            c_label.size(),
            {}
        )
    };

    Render_graph();
    ~Render_graph();

    // Implements Component
    [[nodiscard]] auto get_type_hash() const -> uint32_t override { return hash; }

    // Public API
    [[nodiscard]] auto get_nodes() const -> const std::vector<std::shared_ptr<Render_graph_node>>&;
    void sort         ();
    void execute      ();
    void register_node(const std::shared_ptr<Render_graph_node>& node);

private:
    std::mutex                                      m_mutex;
    std::vector<std::shared_ptr<Render_graph_node>> m_nodes;
};

} // namespace erhe::application
