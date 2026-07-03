#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_physics/irigid_body.hpp"

#include <memory>
#include <string>

namespace erhe::geometry  { class Geometry; }
namespace erhe::physics   { class ICollision_shape; }
namespace erhe::primitive {
    class Material;
    class Primitive;
}
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
//
// Evaluation is two-phase so the graph can be evaluated on a worker
// thread (following the async Mesh_operation pattern: heavy work on the
// worker, scene mutation on the main thread):
// - evaluate() builds the render geometry, the renderable / raytrace
//   primitive and the physics collision shape into m_evaluated_* and
//   never touches the scene, so it is safe on a worker.
// - apply_evaluated_to_scene() consumes m_evaluated_* into the owned
//   scene node; main thread only.
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

    // Applies the products of the latest evaluate() to the owned scene
    // node (creating it on first use). No-op when evaluate() has not run
    // since the last apply. Main thread only.
    void apply_evaluated_to_scene();

    // Moves the evaluated products from a shadow clone of this node onto
    // this (live) node, so a background evaluation's results can be
    // applied here via apply_evaluated_to_scene().
    void take_evaluated(Geometry_output_node& from);

private:
    void remove_scene_node();
    void apply_scene_node_name();

    // Both expect the scene item_host_mutex to be held by the caller
    // when the scene node is attached to a scene.
    void update_node_physics();
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

    // Products of evaluate(), consumed by apply_evaluated_to_scene().
    // A valid result with a null geometry means "input empty or
    // disconnected": apply clears the scene mesh primitives.
    bool                                              m_evaluated_valid{false};
    std::shared_ptr<erhe::geometry::Geometry>         m_evaluated_geometry;
    std::shared_ptr<erhe::primitive::Primitive>       m_evaluated_primitive;
    std::shared_ptr<erhe::physics::ICollision_shape>  m_evaluated_collision_shape;
};

}
