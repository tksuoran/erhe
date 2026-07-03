#pragma once

#include "texture_graph/texture_graph.hpp"

#include "erhe_item/item.hpp"
#include "erhe_graphics/texture.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace editor {

class Texture_graph_node;

// A procedural texture asset backed by a node graph.
//
// Graph_texture is the content-library home of a texture node graph: it owns the
// Texture_graph (its links + evaluation state) and the node objects, and exposes
// the graph's baked output as an erhe::graphics::Texture_reference so a Material
// slot (Material_texture_sampler::texture_source) can sample it. Because the
// renderer resolves the reference every frame and get_referenced_texture()
// returns the current baked output, editing the graph updates every material
// that sources it with no push logic.
//
// The graph is EDITED by Texture_graph_window (the window follows the selected
// Graph_texture); this class only owns the state. It is intentionally
// not_clonable (like erhe::graphics::Texture) - a deep copy would need the node
// factory's App_context, and the content-library duplication path does not
// require it for the MVP; graph duplication is done via serialization instead.
class Graph_texture
    : public erhe::Item<erhe::Item_base, erhe::Item_base, Graph_texture, erhe::Item_kind::not_clonable>
    , public erhe::graphics::Texture_reference
{
public:
    Graph_texture();
    explicit Graph_texture(std::string_view name);
    ~Graph_texture() noexcept override;

    // Implements erhe::Item_base
    static constexpr std::string_view static_type_name{"Graph_texture"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::graph_texture; }

    // Implements erhe::graphics::Texture_reference: the most recently baked
    // texture of this graph's output node, or nullptr when the graph has no
    // usable output (an unbound slot then renders as white).
    [[nodiscard]] auto get_referenced_texture() const -> const erhe::graphics::Texture* override;

    [[nodiscard]] auto graph()       -> Texture_graph&;
    [[nodiscard]] auto graph() const -> const Texture_graph&;
    [[nodiscard]] auto nodes()       -> std::vector<std::shared_ptr<Texture_graph_node>>&;
    [[nodiscard]] auto nodes() const -> const std::vector<std::shared_ptr<Texture_graph_node>>&;

private:
    Texture_graph                                    m_graph;
    std::vector<std::shared_ptr<Texture_graph_node>> m_nodes;
};

} // namespace editor
