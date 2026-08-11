#pragma once

#include "assets/asset_reference.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

namespace erhe::scene { class Node; }

namespace editor {

class App_context;

// Applies the transform of a referenced scene node (the "driver", dropped
// from the item tree onto the node UI) to the input geometry - Transform_node
// with the matrix sourced live from the scene graph instead of authored TRS.
// The driver's transform is captured on the main thread and tracked live
// (update_live), so dragging the driver in the viewport re-poses the
// geometry; shadow clones copy the captured matrix and never touch the
// scene (see doc/geometry-graph-transform-from-node.md).
//
// Space selects which transform is captured:
// - local: the driver's parent-relative transform (matches Lattice_node's
//   driver semantics; parent the driver under the bound mesh node and the
//   pose composes in the graph output's local frame).
// - world: the driver's world transform. The graph output is baked into the
//   bound node's local space, so a world-space driver double-transforms
//   unless the bound node is at identity.
class Transform_from_node : public Geometry_graph_node
{
public:
    enum class Space : int { local = 0, world = 1 };

    explicit Transform_from_node(App_context& context);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;
    void update_live() override;
    void prepare_for_evaluation() override;
    void capture_evaluation_state(const Geometry_graph_node& live_node) override;

    // Binds the driver node and captures its transform (main thread; null clears).
    void set_transform_node(const std::shared_ptr<erhe::scene::Node>& node);

    // The resolved driver (graph-hover -> scene highlight).
    [[nodiscard]] auto get_referenced_scene_node() const -> std::shared_ptr<erhe::scene::Node> override;

private:
    // Main-thread lazy resolution of the stored driver key (scene_local
    // misses do not latch; update_live doubles as the per-frame retry).
    void resolve_transform_reference();
    // Re-captures the driver's transform per m_space; true when it changed.
    auto capture_transform() -> bool;

    App_context&    m_context;
    Asset_reference m_transform_node_reference;
    glm::mat4       m_captured_transform{1.0f};
    Space           m_space             {Space::local};
};

}
