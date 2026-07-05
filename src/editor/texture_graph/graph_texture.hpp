#pragma once

#include "texture_graph/texture_graph.hpp"
#include "graph_editor/graph_asset.hpp"

#include "erhe_item/item.hpp"
#include "erhe_graphics/texture.hpp"

#include <string_view>

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
    : public Graph_asset<Graph_texture, Texture_graph, Texture_graph_node>
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
};

} // namespace editor
