#pragma once

#include "operations/operation.hpp"

#include "erhe_scene/mesh.hpp"

#include <cstddef>
#include <memory>

namespace editor {

// Swaps a single mesh's primitive at one index between a "before" and "after"
// Mesh_primitive. Used by fork-on-edit: 'before' is the shared primitive, 'after'
// is the freshly forked primitive (a deep-copied Geometry referenced only by this
// mesh). The fork is a position-identical copy, so physics (the convex hull) is
// unchanged - the primitive is swapped without rebuilding Node_physics. Queued
// before the Move_mesh_vertices_operation inside the transform's Compound, so undo
// reverts the move first and then un-forks (re-shares) the geometry.
class Fork_geometry_operation : public Operation
{
public:
    class Parameters
    {
    public:
        std::shared_ptr<erhe::scene::Mesh> mesh;
        std::size_t                        primitive_index{0};
        erhe::scene::Mesh_primitive        before; // shared primitive
        erhe::scene::Mesh_primitive        after;  // forked primitive
    };

    explicit Fork_geometry_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

private:
    void apply(App_context& context, const erhe::scene::Mesh_primitive& mesh_primitive);

    Parameters m_parameters;
};

}
