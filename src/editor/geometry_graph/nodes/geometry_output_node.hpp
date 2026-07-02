#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_physics/irigid_body.hpp"

#include <memory>
#include <string>

namespace erhe::geometry  { class Geometry; }
namespace erhe::primitive { class Material; }
namespace erhe::scene {
    class Mesh;
    class Node;
}

namespace editor {

class App_context;
class Node_physics;
class Scene_root;

// Terminal node piping the input geometry into a scene as a renderable
// mesh. Owns one erhe::scene::Node with an attached erhe::scene::Mesh
// which is updated in place on every re-evaluation; the scene node is
// removed when this graph node is deleted.
//
// Target scene defaults to the single registered scene root; when
// several scenes exist a stepper selects one. Material defaults to the
// first material in the scene's content library.
//
// Optional physics: when enabled, a Node_physics attachment with a
// convex hull collision shape (built from the render geometry, so
// non-convex results are approximated by their hull) is kept in sync
// with the mesh on every re-evaluation.
class Geometry_output_node : public Geometry_graph_node
{
public:
    explicit Geometry_output_node(App_context& context);
    ~Geometry_output_node() noexcept override;

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void on_removed_from_graph() override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

private:
    void remove_scene_node();
    void apply_scene_node_name();

    // Both expect the scene item_host_mutex to be held by the caller
    // when the scene node is attached to a scene.
    void update_node_physics(const std::shared_ptr<erhe::geometry::Geometry>& render_geometry);
    void remove_node_physics();

    App_context&                               m_context;
    std::shared_ptr<Scene_root>                m_scene_root;
    std::shared_ptr<erhe::primitive::Material> m_material;
    std::shared_ptr<erhe::scene::Node>         m_node;
    std::shared_ptr<erhe::scene::Mesh>         m_mesh;
    std::shared_ptr<Node_physics>              m_node_physics;
    std::string                                m_scene_node_name{"Geometry Graph"};
    bool                                       m_physics_enabled{false};
    erhe::physics::Motion_mode                 m_physics_motion_mode{erhe::physics::Motion_mode::e_static};
};

}
