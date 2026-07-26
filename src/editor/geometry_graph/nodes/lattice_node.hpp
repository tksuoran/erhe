#pragma once

#include "assets/asset_reference.hpp"
#include "geometry_graph/geometry_graph_node.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace erhe::scene { class Node; }

namespace editor {

class App_context;

// Free-form deformation through a regular control point lattice (Houdini
// Lattice SOP style). The cage is an axis aligned box - auto fit to the
// input geometry or set manually - and the node owns the control point
// offsets, edited one point at a time in the node UI.
//
// An optional transform-driver scene node (dropped from the item tree onto
// the node UI) defines the CAGE FRAME: the cage box and the offsets live in
// the driver's space, so moving the driver moves the cage relative to the
// geometry (repositioning the deformation region - zero offsets deform
// nothing in any frame) and the authored offsets apply within that frame.
// The driver's LOCAL (parent-relative) transform is captured on the main
// thread and tracked live (update_live); parent the driver under the bound
// scene node so the cage rides with the mesh.
class Lattice_node : public Geometry_graph_node
{
public:
    explicit Lattice_node(App_context& context);

    void evaluate(Geometry_graph&) override;
    void imgui   () override;
    void write_parameters(nlohmann::json& out) const override;
    void read_parameters (const nlohmann::json& in) override;
    void update_live() override;
    void prepare_for_evaluation() override;
    void capture_evaluation_state(const Geometry_graph_node& live_node) override;

    // Viewport editing API (Lattice_tool / Lattice_point_transform). Main
    // thread, live node only.
    [[nodiscard]] auto get_divisions() const -> glm::ivec3 { return m_divisions; }
    [[nodiscard]] auto get_selected_point() const -> glm::ivec3 { return m_selected_point; }
    void set_selected_point(glm::ivec3 point); // clamped; UI selection only, does not dirty the graph
    [[nodiscard]] auto get_control_point_offset(glm::ivec3 point) const -> glm::vec3;
    void set_control_point_offset(glm::ivec3 point, glm::vec3 offset); // marks the node dirty
    // The effective cage exactly as evaluate() uses it (auto fit from the
    // input geometry + degenerate axis padding). False when auto fit has no
    // input geometry payload to fit against.
    [[nodiscard]] auto resolve_cage(glm::vec3& out_cage_min, glm::vec3& out_cage_max) const -> bool;
    // The captured cage frame: the driver's local transform (identity when
    // no driver node is set / resolved). Control point consumers (viewport
    // tool, gizmo) place points at T * (rest + offset); offsets are stored
    // in cage space.
    [[nodiscard]] auto get_control_point_transform() const -> const glm::mat4& { return m_captured_transform; }
    // Binds the driver node and captures its transform (main thread; null clears).
    void set_transform_node(const std::shared_ptr<erhe::scene::Node>& node);

private:
    // Main-thread lazy resolution of the stored driver key (scene_local
    // misses do not latch; update_live doubles as the per-frame retry).
    void resolve_transform_reference();
    // Re-captures the driver's world transform; true when it changed.
    auto capture_transform() -> bool;
    [[nodiscard]] auto has_deformation() const -> bool;
    // Rebuilds m_offsets for m_divisions, resampling the previous offset
    // field (trilinearly) so an authored deformation survives a resolution
    // change. The single place that resizes m_offsets.
    void resample_offsets(glm::ivec3 old_divisions, const std::vector<glm::vec3>& old_offsets);

    static constexpr int max_divisions = 16;

    App_context&           m_context;
    Asset_reference        m_transform_node_reference;
    glm::mat4              m_captured_transform   {1.0f};
    bool                   m_auto_fit             {true};
    glm::vec3              m_cage_min             {-1.0f, -1.0f, -1.0f}; // used when !m_auto_fit
    glm::vec3              m_cage_max             { 1.0f,  1.0f,  1.0f};
    glm::ivec3             m_divisions            {2, 2, 2};
    int                    m_interpolation        {0}; // erhe::geometry::operation::Lattice_interpolation
    std::vector<glm::vec3> m_offsets;
    bool                   m_regenerate_attributes{true};
    bool                   m_show_cage            {true};
    glm::ivec3             m_selected_point       {0, 0, 0}; // UI selection only - not serialized, does not affect evaluation
};

}
