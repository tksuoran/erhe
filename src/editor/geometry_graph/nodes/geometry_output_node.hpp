#pragma once

#include "assets/asset_reference.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include "erhe_physics/irigid_body.hpp"
#include "erhe_property/dependency_property.hpp"

#include <memory>
#include <string>

namespace erhe::geometry  { class Geometry; }
namespace erhe::physics   { class ICollision_shape; }
namespace erhe::primitive {
    class Material;
    class Primitive;
}

namespace editor {

class App_context;

// Terminal node publishing the input geometry as the owning Graph_mesh
// asset's baked products (render geometry, renderable / raytrace
// primitive, material, optional convex-hull collision shape + physics
// flags). The node never creates scene content itself: the nodes bound
// to the asset consume the published bake, and an asset with no bound
// scene node renders nothing - exactly like a Graph_texture no material
// samples.
//
// Material defaults to the first material in the scene's content
// library (resolved by the scene's geometry-graph-mesh system when unset
// here).
//
// Evaluation is two-phase so the graph can be evaluated on a worker
// thread (following the async Mesh_operation pattern: heavy work on the
// worker, scene mutation on the main thread):
// - evaluate() builds the render geometry, the renderable / raytrace
//   primitive and the physics collision shape into m_evaluated_* and
//   never touches the scene, so it is safe on a worker.
// - apply_evaluated_to_scene() publishes m_evaluated_* to the owning
//   Graph_mesh asset; main thread only.
class Geometry_output_node : public Geometry_graph_node
{
public:
    explicit Geometry_output_node(App_context& context);
    ~Geometry_output_node() noexcept override;

    void evaluate(Geometry_graph&) override;
    [[nodiscard]] auto is_scene_output() const -> bool override;
    void imgui   () override;
    void on_removed_from_graph() override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;

    // Registered parameter properties (D18 / D27; Mesh_torus_node is the
    // recipe). The material Asset_reference is not a property and draws
    // through unregistered_parameters_imgui().
    [[nodiscard]] static auto property_owner_type() -> erhe::property::Owner_type;
    [[nodiscard]] auto get_property_owner_type() const -> erhe::property::Owner_type override;
    [[nodiscard]] auto has_unregistered_parameters() const -> bool override { return true; }
    void unregistered_parameters_imgui(App_context& context) override;
    static const erhe::property::Property<std::string>                name_property;
    static const erhe::property::Property<bool>                       physics_property;
    static const erhe::property::Property<erhe::physics::Motion_mode> physics_motion_property;

    // Publishes the products of the latest evaluate() to the owning
    // Graph_mesh asset. No-op when evaluate() has not run since the last
    // apply. Main thread only.
    void apply_evaluated_to_scene();

    // Moves the evaluated products from a shadow clone of this node onto
    // this (live) node, so a background evaluation's results can be
    // applied here via apply_evaluated_to_scene().
    void take_evaluated(Geometry_output_node& from);

private:
    // Main-thread lazy resolution of the stored material key through the
    // asset manager (R4: read_parameters only stores the key).
    void resolve_reference();
    // The material picker (reference retry + index stepper), shared by the
    // canvas imgui() and unregistered_parameters_imgui().
    void material_imgui();

    App_context&                               m_context;
    Asset_reference                            m_material_reference;
    std::string                                m_name{"Geometry Graph"};
    bool                                       m_physics_enabled{false};
    erhe::physics::Motion_mode                 m_physics_motion_mode{erhe::physics::Motion_mode::e_static};

    // Products of evaluate(), consumed by apply_evaluated_to_scene().
    // A valid result with a null geometry means "input empty or
    // disconnected": apply clears the scene mesh primitives.
    bool                                              m_evaluated_valid{false};
    std::shared_ptr<erhe::geometry::Geometry>         m_evaluated_geometry;
    std::shared_ptr<erhe::primitive::Primitive>       m_evaluated_primitive;
    std::shared_ptr<erhe::physics::ICollision_shape>  m_evaluated_collision_shape;
    // Ghost-node products (edge-lines-only companion mesh; no raytrace,
    // no physics). Null when no ghost node is designated.
    std::shared_ptr<erhe::geometry::Geometry>         m_evaluated_ghost_geometry;
    std::shared_ptr<erhe::primitive::Primitive>       m_evaluated_ghost_primitive;
};

}
