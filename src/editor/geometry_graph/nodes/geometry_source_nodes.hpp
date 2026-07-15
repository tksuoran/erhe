#pragma once

#include "geometry_graph/geometry_graph_node.hpp"

namespace erhe::scene { class Mesh; }

namespace editor {

class App_context;
class Brush;

// Geometry source nodes: provide existing editor content (a content-library
// brush, a scene mesh) as a geometry output usable as input for other nodes.
//
// Both nodes resolve their referenced item and CAPTURE ITS GEOMETRY on the
// main thread (picker / drag-drop / read_parameters - Brush::get_geometry()
// and Primitive_shape::get_geometry() may lazily build, which is not safe on
// the evaluation worker). evaluate() then only copies the captured geometry
// (the source is shared and must not be mutated by process_for_graph), so the
// shadow-clone snapshot (write_parameters -> read_parameters on the main
// thread in launch_evaluation) re-resolves and the worker never touches the
// referenced item. The reference is serialized by name; resolution prefers
// the owning asset's scene and falls back to every scene (shadow clones have
// no owning graph mesh).
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

    // Binds the brush and captures its geometry (main thread; used by the
    // canvas drag-drop and the picker).
    void set_brush(const std::shared_ptr<Brush>& brush);

private:
    void resolve_brush_by_name(const std::string& name);

    App_context&                              m_context;
    std::shared_ptr<Brush>                    m_brush;
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

    // Binds the mesh and captures its first primitive's geometry (main
    // thread). The geometry is the mesh's local-space shape; the scene
    // node's transform is not applied.
    void set_mesh(const std::shared_ptr<erhe::scene::Mesh>& mesh);

private:
    void resolve_mesh_by_name(const std::string& name);

    App_context&                              m_context;
    std::shared_ptr<erhe::scene::Mesh>        m_mesh;
    std::shared_ptr<erhe::geometry::Geometry> m_source_geometry;
};

}
