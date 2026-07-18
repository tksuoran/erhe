#pragma once

#include "assets/asset_reference.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include <vector>

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;
class Brush;

// Geometry source nodes: provide existing editor content (a content-library
// brush, a scene mesh) as a geometry output usable as input for other nodes.
//
// The reference is an Asset_reference (R4): read_parameters only stores the
// scene_local key (worker-safe - no manager access), and the key is written
// back on save even while unresolved, so a reference that never resolved
// this session survives the next save. Resolution goes through the asset
// manager on the main thread (prepare_for_evaluation before each snapshot;
// imgui retries per frame while visible) and CAPTURES THE GEOMETRY there
// (Brush::get_geometry() and Primitive_shape::get_geometry() may lazily
// build, which is not safe on the evaluation worker). evaluate() then only
// copies the captured geometry (the source is shared and must not be mutated
// by process_for_graph). Shadow clones receive the captured geometry via
// capture_evaluation_state() and never resolve or hold userships.
//
// The captured geometry is a snapshot: editing the brush / mesh afterwards
// does not re-evaluate the graph. The node's Refresh button re-captures.

class Brush_geometry_node : public Geometry_graph_node
{
public:
    explicit Brush_geometry_node(App_context& context);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;
    void prepare_for_evaluation() override;
    void capture_evaluation_state(const Geometry_graph_node& live_node) override;

    // Binds the brush and captures its geometry (main thread; used by the
    // canvas drag-drop and the picker).
    void set_brush(const std::shared_ptr<Brush>& brush);

private:
    // Main-thread lazy resolution of the stored key through the asset
    // manager; captures the geometry on success (scene_local misses do not
    // latch, so calls double as the retry).
    void resolve_reference();

    App_context&                              m_context;
    Asset_reference                           m_brush_reference;
    std::shared_ptr<erhe::geometry::Geometry> m_source_geometry;
};

class Scene_mesh_geometry_node : public Geometry_graph_node
{
public:
    explicit Scene_mesh_geometry_node(App_context& context);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;
    void prepare_for_evaluation() override;
    void capture_evaluation_state(const Geometry_graph_node& live_node) override;

    // Binds the mesh and captures its primitives' geometries (main thread).
    // The geometry is the mesh's local-space shape; the scene node's
    // transform is not applied.
    void set_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh);

private:
    // See Brush_geometry_node::resolve_reference().
    void resolve_reference();
    // Captures the resolved mesh's primitive geometries (main thread), one
    // entry per mesh primitive (null where a primitive has no render-shape
    // geometry), so evaluate() can index by the "primitive" input off the
    // main thread.
    void capture_source_geometry();

    App_context&                                           m_context;
    Asset_reference                                        m_mesh_reference;
    std::vector<std::shared_ptr<erhe::geometry::Geometry>> m_source_geometries;
    int                                                    m_primitive_index{0}; // fallback when the "primitive" pin is unlinked
};

}
