#pragma once

#include "erhe_item/item.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace editor {

// Shared base for the content-library graph assets (Graph_texture, Graph_mesh).
//
// A graph asset owns a node graph: the GraphT (its links + evaluation state) and
// the concrete node objects. It is an erhe::Item, lives in a Content_library
// folder, shows in the UI, and is not_clonable (a deep copy would need the node
// factory's App_context; asset duplication goes through serialization).
//
// This base only unifies the ownership + Item identity that both assets share;
// how the graph's output is CONSUMED stays in the derived class - Graph_texture
// exposes a pull erhe::graphics::Texture_reference, Graph_mesh publishes push
// baked products to its bound attachments.
//
// Self is threaded to erhe::Item so get_type() / get_type_name() resolve to the
// concrete asset's get_static_type() / static_type_name.
template <typename Self, typename GraphT, typename NodeT>
class Graph_asset : public erhe::Item<erhe::Item_base, erhe::Item_base, Self, erhe::Item_kind::not_clonable>
{
public:
    explicit Graph_asset(std::string_view name)
        : erhe::Item<erhe::Item_base, erhe::Item_base, Self, erhe::Item_kind::not_clonable>{name}
    {
        this->enable_flag_bits(erhe::Item_flags::show_in_ui | erhe::Item_flags::content);
    }

    [[nodiscard]] auto graph()       -> GraphT&       { return m_graph; }
    [[nodiscard]] auto graph() const -> const GraphT& { return m_graph; }
    [[nodiscard]] auto nodes()       -> std::vector<std::shared_ptr<NodeT>>&       { return m_nodes; }
    [[nodiscard]] auto nodes() const -> const std::vector<std::shared_ptr<NodeT>>& { return m_nodes; }

protected:
    GraphT                              m_graph;
    std::vector<std::shared_ptr<NodeT>> m_nodes;
};

} // namespace editor
