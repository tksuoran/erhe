#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/operation.hpp"

#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_primitive/build_info.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"

#include <geogram/basic/numeric.h>

#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace editor {

class App_context;

// Separate selection (Blender P, doc/plans/mesh_modeling.md catalog M9): the
// facets of each mesh's component selection become a new Mesh beside the
// original - same parent, the sibling index right after the original, the
// same transform, the same material per primitive, the same flags, named
// "<original> separated" - and the original loses them
// (erhe::geometry::operation::extract_facets()). Both halves keep every
// attribute; one undo entry restores both.
//
// The constructor runs on the main thread and builds both geometries and
// their primitives up front. execute() swaps the kept primitives into the
// original and inserts the new mesh; undo() removes the new mesh (its parent
// and sibling index are remembered for redo) and swaps the original's
// primitives back. Physics: the original keeps its motion mode on a convex
// hull of the kept geometry (none when that has no volume), like any
// Mesh_operation; the new mesh gets a body only when the original's is static
// (a static body on the convex hull of the extracted geometry, when that has
// volume). The object selection is unchanged; the component selection of the
// original's new geometry is empty (the old entry goes dormant and undo
// revives it).
class Separate_selection_operation : public Operation
{
public:
    class Parameters
    {
    public:
        App_context&                                  context;
        // The object selection's operands; each mesh with facets in the
        // snapshot is separated.
        std::vector<std::shared_ptr<erhe::Item_base>> items;
        erhe::primitive::Build_info                   build_info;
        // The mesh-component selection snapshot (snapshot_component_selection()):
        // the facets of get_selection_facets() of each Geometry's selection
        // are separated.
        std::unordered_map<const erhe::geometry::Geometry*, erhe::geometry::operation::Geometry_component_selection> component_selection;
    };

    explicit Separate_selection_operation(Parameters&& parameters);

    // Implements Operation
    void execute(App_context& context) override;
    void undo   (App_context& context) override;

    // True when no mesh had facets to separate (the operation does nothing).
    [[nodiscard]] auto is_empty() const -> bool;
    // The new meshes, one per separated original, in item order.
    [[nodiscard]] auto get_separated_meshes() const -> std::vector<std::shared_ptr<erhe::scene::Mesh>>;

private:
    class Entry
    {
    public:
        std::shared_ptr<erhe::scene::Mesh>       original;
        std::shared_ptr<erhe::scene::Mesh>       separated;
        std::vector<erhe::scene::Mesh_primitive> original_before;
        std::vector<erhe::scene::Mesh_primitive> original_after;
        Mesh_operation::Entry::Version           original_physics_before;
        Mesh_operation::Entry::Version           original_physics_after;
        Mesh_operation::Entry::Version           separated_physics;
        // Where undo removed the new mesh from, for redo; empty before the
        // first undo (execute then places it after the original).
        std::shared_ptr<erhe::scene::Node>       separated_parent;
        std::size_t                              separated_index_in_parent{0};
    };

    Parameters         m_parameters;
    std::vector<Entry> m_entries;
};

} // namespace editor
