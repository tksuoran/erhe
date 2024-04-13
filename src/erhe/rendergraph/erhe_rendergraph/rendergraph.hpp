#pragma once

#include <mutex>
#include <vector>

namespace igl {
    class IDevice;
    class ITexture;
}

namespace erhe::rendergraph {

class Rendergraph_node;

class Rendergraph final
{
public:
    explicit Rendergraph(igl::IDevice& graphics_instance);
    ~Rendergraph();

    // Public API
    [[nodiscard]] auto get_nodes() const -> const std::vector<Rendergraph_node*>&;
    void sort           ();
    void execute        ();
    void register_node  (Rendergraph_node* node);
    void unregister_node(Rendergraph_node* node);

    auto connect(int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;
    auto disconnect(int key, Rendergraph_node* source_node, Rendergraph_node* sink_node) -> bool;
    [[nodiscard]] auto get_device() -> igl::IDevice&;

    void automatic_layout(float image_size);

    float x_gap{100.0f};
    float y_gap{100.0f};

private:
    igl::IDevice&                  m_device;
    std::mutex                     m_mutex;
    std::vector<Rendergraph_node*> m_nodes;
};

} // namespace erhe::rendergraph
