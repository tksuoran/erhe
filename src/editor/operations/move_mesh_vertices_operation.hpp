#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/mesh_primitive_swap.hpp"
#include "operations/operation.hpp"

#include "erhe_primitive/build_info.hpp"
#include "erhe_primitive/enums.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>
#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace erhe::geometry { class Geometry; }
namespace erhe::scene    { class Mesh; }

namespace editor {

class App_context;

// One corner texcoord a vertex move re-interpolates (the slides' "correct UVs",
// doc/editor/transform.md): the value of corner_texcoord_<set> at `corner`
// before and after the move.
class Corner_texcoord_change
{
public:
    GEO::index_t  corner{0};
    std::uint32_t set   {0}; // texcoord set 0 .. 2
    glm::vec2     before{0.0f};
    glm::vec2     after {0.0f};
};

// The normal attributes a vertex move recomputes, element by element as
// stored (value and present flag), so undo puts them back bit for bit.
class Geometry_normals_snapshot
{
public:
    void capture(const erhe::geometry::Geometry& geometry);
    void restore(erhe::geometry::Geometry& geometry) const;
    void clear  ();

private:
    std::vector<GEO::vec3f> m_facet_normal;
    std::vector<bool>       m_facet_normal_present;
    std::vector<GEO::vec3f> m_vertex_normal;
    std::vector<bool>       m_vertex_normal_present;
    std::vector<GEO::vec3f> m_vertex_normal_smooth;
    std::vector<bool>       m_vertex_normal_smooth_present;
    std::vector<GEO::vec3f> m_corner_normal;
    std::vector<bool>       m_corner_normal_present;
};

// Undo-able edit that moves a set of geometry vertices of a single mesh
// primitive (and, for a slide, re-interpolated corner texcoords of the facets
// around them). It edits the same Geometry object in place (doc/editor/
// operations.md "In-place vertex edits"); undo restores the primitives every
// sharing mesh held before it (doc/editor/operations.md "Undo of an in-place edit").
class Move_mesh_vertices_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>        mesh{};
        std::size_t                               primitive_index{0};
        std::shared_ptr<erhe::geometry::Geometry> geometry{};
        std::vector<GEO::index_t>                 vertices{};          // affected geometry vertices
        std::vector<glm::vec3>                    before_positions{};  // mesh-local, parallel to vertices
        std::vector<glm::vec3>                    after_positions{};   // mesh-local, parallel to vertices
        erhe::primitive::Build_info               build_info;
        erhe::primitive::Normal_style             normal_style{erhe::primitive::Normal_style::corner_normals};
        // Corner texcoords the move re-interpolates (empty for a plain move).
        std::vector<Corner_texcoord_change>       corner_texcoords{};
        // Undo stack label; empty: "Move <n> mesh vertices".
        std::string                               description{};
    };

    explicit Move_mesh_vertices_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    enum class State : unsigned int { before = 0, after = 1 };
    // Writes the positions and texcoords of `state` into the Geometry; the caller holds the item host mutex.
    void write_geometry(State state);

    Parameters m_parameters;
    // Held from execute to undo: what the edit replaced.
    Geometry_normals_snapshot                    m_before_normals;
    std::vector<Retained_mesh_primitives>        m_before_primitives;
    std::vector<Mesh_operation::Entry::Version>  m_before_physics; // parallel to m_before_primitives
};

}
