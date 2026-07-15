#pragma once

#include "geometry_graph/geometry_graph.hpp"
#include "graph_editor/graph_asset.hpp"

#include "erhe_item/item.hpp"
#include "erhe_physics/irigid_body.hpp"

#include <memory>
#include <string_view>
#include <vector>

namespace erhe::geometry  { class Geometry; }
namespace erhe::physics   { class ICollision_shape; }
namespace erhe::primitive {
    class Material;
    class Primitive;
}

namespace editor {

class Geometry_graph_node;

// The products of the most recent evaluation of a Graph_mesh's output
// node: everything a bound Geometry_graph_mesh attachment needs to
// materialize the graph's result on its scene node. All heavy members
// are shared - N attachments bound to the same asset share one GPU
// primitive. A set revision with a null geometry means the graph
// evaluated to "empty / disconnected" (attachments clear their mesh).
class Graph_mesh_baked_products
{
public:
    std::shared_ptr<erhe::geometry::Geometry>        geometry;
    std::shared_ptr<erhe::primitive::Primitive>      primitive;
    std::shared_ptr<erhe::primitive::Material>       material;
    std::shared_ptr<erhe::physics::ICollision_shape> collision_shape;
    bool                                             physics_enabled{false};
    erhe::physics::Motion_mode                       physics_motion_mode{erhe::physics::Motion_mode::e_static};
    // The graph's ghost node (Houdini template flag), baked as an
    // edge-lines-only companion mesh: no raytrace / picking, no shadow,
    // no physics. Null when no ghost node is designated (attachments
    // then clear their ghost mesh).
    std::shared_ptr<erhe::geometry::Geometry>        ghost_geometry;
    std::shared_ptr<erhe::primitive::Primitive>      ghost_primitive;
};

// A procedural mesh asset backed by a geometry node graph.
//
// Graph_mesh is the content-library home of a geometry node graph: it owns
// the Geometry_graph (links + evaluation state) and the node objects, plus
// the baked products its output node publishes after each (background)
// evaluation. Scene nodes consume the asset through the Geometry_graph_mesh
// Node_attachment, which points back here and swaps its controlled Mesh's
// primitives whenever the baked revision advances - the scene-side analogue
// of Graph_texture + Material::texture_reference.
//
// The graph is EDITED by Geometry_graph_window (the window follows the
// selected Graph_mesh); this class only owns the state. It is intentionally
// not_clonable (like Graph_texture) - a deep copy would need the node
// factory's App_context; graph duplication goes through serialization.
class Graph_mesh
    : public Graph_asset<Graph_mesh, Geometry_graph, Geometry_graph_node>
{
public:
    Graph_mesh();
    explicit Graph_mesh(std::string_view name);
    ~Graph_mesh() noexcept override;

    // Implements erhe::Item_base
    static constexpr std::string_view static_type_name{"Graph_mesh"};
    [[nodiscard]] static constexpr auto get_static_type() -> uint64_t { return erhe::Item_type::graph_mesh; }

    // Published by the asset-owned Geometry_output_node from
    // apply_evaluated_to_scene() (main thread); consumed by bound
    // Geometry_graph_mesh attachments. Every publish advances the
    // revision so late binders can apply the latest bake immediately.
    void set_baked_products(const Graph_mesh_baked_products& products);
    [[nodiscard]] auto get_baked_products() const -> const Graph_mesh_baked_products&;
    [[nodiscard]] auto get_baked_revision() const -> uint64_t;

    // Out-of-band request to re-push the current baked products to bound
    // attachments (set when a bound node re-enters a scene after missing
    // a push, or when the output node leaves the graph). Consumed by
    // Geometry_graph_window::update_evaluation() each frame.
    void request_attachment_push();
    [[nodiscard]] auto consume_attachment_push_request() -> bool;

    // Per-node mesh preview thumbnails are an editor-global setting now
    // (Editor_settings_config::graph_node_previews); see
    // Geometry_graph_window::set_node_previews_enabled.

private:
    Graph_mesh_baked_products                         m_baked_products;
    uint64_t                                          m_baked_revision{0};
    bool                                              m_attachment_push_requested{false};
};

} // namespace editor
