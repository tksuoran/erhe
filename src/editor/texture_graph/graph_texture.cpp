#include "texture_graph/graph_texture.hpp"
#include "texture_graph/texture_graph_node.hpp"
#include "texture_graph/nodes/texture_output_node.hpp"

#include "erhe_graph/link.hpp"
#include "erhe_graph/node.hpp"

#include <algorithm>

namespace editor {

Graph_texture::Graph_texture()
    : Graph_asset{"Graph Texture"}
{
}

Graph_texture::Graph_texture(const std::string_view name)
    : Graph_asset{name}
{
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

void Graph_texture::add_user(erhe::graphics::Texture_reference_user& user)
{
    // A material that binds the same graph into two slots registers twice and
    // unregisters twice, so the list holds one entry per binding.
    m_users.push_back(&user);
}

void Graph_texture::remove_user(erhe::graphics::Texture_reference_user& user)
{
    const std::vector<erhe::graphics::Texture_reference_user*>::iterator i = std::find(m_users.begin(), m_users.end(), &user);
    if (i != m_users.end()) {
        m_users.erase(i);
    }
}

void Graph_texture::notify_referenced_texture_changed()
{
    for (erhe::graphics::Texture_reference_user* user : m_users) {
        user->on_referenced_texture_changed();
    }
}

} // namespace editor
