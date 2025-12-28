#pragma once

#include "erhe_profile/profile.hpp"

#include <mutex>
#include <vector>

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

    float x_gap{100.0f};
    float y_gap{100.0f};

private:
    erhe::graphics::Device&        m_graphics_device;
    ERHE_PROFILE_MUTEX(std::mutex, m_mutex);
    std::vector<Rendergraph_node*> m_nodes;
};

} // namespace erhe::rendergraph
