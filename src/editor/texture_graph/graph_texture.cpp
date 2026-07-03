#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/nodes/texture_output_node.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"

namespace editor {

Graph_texture::Graph_texture()
    : Item{"Graph Texture"}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
}

Graph_texture::Graph_texture(const std::string_view name)
    : Item{name}
{
    enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
}

Graph_texture::~Graph_texture() noexcept = default;

auto Graph_texture::get_referenced_texture() const -> const erhe::graphics::Texture*
{
    // The graph's output is whatever its (first) output node has baked. Material
    // and other multi-texture sinks are not exposed as a single reference.
    for (const std::shared_ptr<Texture_graph_node>& node : m_nodes) {
        const Texture_output_node* output_node = dynamic_cast<const Texture_output_node*>(node.get());
        if (output_node != nullptr) {
            const erhe::graphics::Texture* texture = output_node->get_baked_texture();
            if (texture != nullptr) {
                return texture;
            }
        }
    }
    return nullptr;
}

auto Graph_texture::graph() -> Texture_graph&
{
    return m_graph;
}

auto Graph_texture::graph() const -> const Texture_graph&
{
    return m_graph;
}

auto Graph_texture::nodes() -> std::vector<std::shared_ptr<Texture_graph_node>>&
{
    return m_nodes;
}

auto Graph_texture::nodes() const -> const std::vector<std::shared_ptr<Texture_graph_node>>&
{
    return m_nodes;
}

} // namespace editor
