#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/compound_operation.hpp"

#include "erhe_geometry/operation/bridge_loops.hpp"
#include "erhe_geometry/operation/dissolve.hpp"
#include "erhe_geometry/operation/flip_facets.hpp"
#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_geometry/operation/make_atlas.hpp"
#include "erhe_geometry/operation/merge_vertices.hpp"
#include "erhe_geometry/operation/project_texcoords.hpp"
#include "erhe_geometry/operation/split_components.hpp"
#include "erhe_geometry/operation/subdivide_edges.hpp"

#include <glm/glm.hpp>

#include <optional>
#include <unordered_map>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

// post_process_flags: exact Geometry::process_flag_* set the subdivision's
// post-processing runs (must include connect + build_edges + centroids); the
// Operations window composes it from the Generate UVs checkbox.
class Catmull_clark_subdivision_operation : public Mesh_operation
{
public:
    Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context, uint64_t post_process_flags);
};

// post_process_flags: see Catmull_clark_subdivision_operation.
class Sqrt3_subdivision_operation : public Mesh_operation
{
public:
    Sqrt3_subdivision_operation(Mesh_operation_parameters&& context, uint64_t post_process_flags);
};

class Triangulate_operation : public Mesh_operation
{
public:
    explicit Triangulate_operation(Mesh_operation_parameters&& context);
};

class Join_operation : public Mesh_operation
{
public:
    explicit Join_operation(Mesh_operation_parameters&& context);
};

class Kis_operation : public Mesh_operation
{
public:
    Kis_operation(Mesh_operation_parameters&& context, float height);
};

class Subdivide_operation : public Mesh_operation
{
public:
    explicit Subdivide_operation(Mesh_operation_parameters&& context);
};

class Meta_operation : public Mesh_operation
{
public:
    explicit Meta_operation(Mesh_operation_parameters&& context);
};

class Gyro_operation : public Mesh_operation
{
public:
    Gyro_operation(Mesh_operation_parameters&& context, float ratio);
};

class Chamfer3_operation : public Mesh_operation
{
public:
    Chamfer3_operation(Mesh_operation_parameters&& context, float bevel_ratio);
};

class Dual_operation : public Mesh_operation
{
public:
    explicit Dual_operation(Mesh_operation_parameters&& context);

};

class Ambo_operation : public Mesh_operation
{
public:
    explicit Ambo_operation(Mesh_operation_parameters&& context);
};

class Truncate_operation : public Mesh_operation
{
public:
    Truncate_operation(Mesh_operation_parameters&& context, float ratio);
};

class Merge_faces_operation : public Mesh_operation
{
public:
    explicit Merge_faces_operation(Mesh_operation_parameters&& context);
};

// Delete and dissolve (doc/plans/mesh_modeling.md section 4.3,
// erhe_geometry/operation/dissolve.hpp). Each acts on the mesh-component
// selection snapshot of Mesh_operation_parameters::component_selection (the
// active mode's set) and carries the selection over to the result; a
// primitive without a selection in the snapshot is emitted unchanged.
class Delete_components_operation : public Mesh_operation
{
public:
    Delete_components_operation(Mesh_operation_parameters&& context, erhe::geometry::Delete_context delete_context);
};

class Dissolve_faces_operation : public Mesh_operation
{
public:
    Dissolve_faces_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Dissolve_faces_options options);
};

class Dissolve_edges_operation : public Mesh_operation
{
public:
    Dissolve_edges_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Dissolve_edges_options options);
};

class Dissolve_vertices_operation : public Mesh_operation
{
public:
    Dissolve_vertices_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Dissolve_vertices_options options);
};

// Limited dissolve on the component selection when the snapshot holds one,
// else on the whole of every mesh of the object selection.
class Dissolve_limited_operation : public Mesh_operation
{
public:
    Dissolve_limited_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Dissolve_limited_options options);
};

// Merge (doc/plans/mesh_modeling.md section 4.4,
// erhe_geometry/operation/merge_vertices.hpp) on the component selection
// snapshot; the library derives the merged vertices from any mode's set (the
// endpoints of edges, the vertices of facets). With world_position set, the
// at_position target is that world point converted into each mesh's local
// space (the component tool's hovered point); otherwise options.position is
// used as given (mesh-local). A primitive without a selection is emitted
// unchanged.
class Merge_vertices_operation : public Mesh_operation
{
public:
    Merge_vertices_operation(
        Mesh_operation_parameters&&                       context,
        erhe::geometry::operation::Merge_vertices_options options,
        std::optional<glm::vec3>                          world_position = std::nullopt
    );
};

// Merge by distance on the component selection when the snapshot holds one,
// else on the whole of every mesh of the object selection.
class Merge_by_distance_operation : public Mesh_operation
{
public:
    Merge_by_distance_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Merge_by_distance_options options);
};

// Subdivide edges (doc/plans/mesh_modeling.md section 4.5,
// erhe_geometry/operation/subdivide_edges.hpp) on the component selection
// snapshot: the edges of the selection's set, whichever mode it comes from
// (edge mode: the selected edges; face mode: every edge of the selected
// facets; vertex mode: every edge between two selected vertices). After the
// operation, an edge-mode selection is the new inner edges (the edges the
// fills created, as loop cut needs) when there are any, else the halves of
// the split edges; a vertex- or face-mode selection follows the general
// remap (the selected vertices; the facets descended from the selected
// facets). A primitive without a selection is emitted unchanged.
class Subdivide_edges_operation : public Mesh_operation
{
public:
    Subdivide_edges_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Subdivide_edges_options options);
};

// Split (doc/plans/mesh_modeling.md catalog M9,
// erhe_geometry/operation/split_components.hpp) on the component selection
// snapshot. Face mode: the selected facets are split off the rest (region
// split); vertex mode: the facets whose vertices are all selected; edge mode:
// the facets whose edges are all selected, or, when the selected edges hold no
// complete facet, an edge split along the selected edges. The selection
// afterwards is the region (or the torn side's copies of the edges). A
// primitive without a selection is emitted unchanged.
class Split_components_operation : public Mesh_operation
{
public:
    explicit Split_components_operation(Mesh_operation_parameters&& context);
};

// Rip (catalog M9) on a vertex or edge mode selection snapshot
// (erhe::geometry::operation::rip_vertices()). With world_position, each
// mesh rips the side toward that point: the direction is the point in the
// mesh's local space minus the centroid of the torn vertices; without it,
// options.direction (mesh-local) is used as given. The selection afterwards
// is the ripped vertices (vertex mode) or the ripped copies of the edges
// (edge mode).
class Rip_vertices_operation : public Mesh_operation
{
public:
    Rip_vertices_operation(
        Mesh_operation_parameters&&            context,
        erhe::geometry::operation::Rip_options options,
        std::optional<glm::vec3>               world_position = std::nullopt
    );
};

// Fill (F, doc/plans/mesh_modeling.md section 4.10, catalog M15;
// erhe::geometry::operation::fill_selection()) on the component selection
// snapshot of any mode: two vertices closing a boundary chain, a free vertex
// plus a chain, edge cycles / chains / an edge net, selected faces dissolved,
// or three or more vertices sorted radially, the first case that creates
// anything. The new facets are selected afterwards. With nothing to fill the
// primitive is emitted unchanged (logged).
class Fill_operation : public Mesh_operation
{
public:
    explicit Fill_operation(Mesh_operation_parameters&& context);
};

// Connect vertex path (J, catalog M16;
// erhe::geometry::operation::connect_selection()) on the component selection
// snapshot: two vertices sharing no facet are joined along the cutting plane
// path, otherwise each facet is split between its selected corners. The new
// edges (and the selected and inserted vertices) are selected afterwards.
class Connect_vertices_operation : public Mesh_operation
{
public:
    explicit Connect_vertices_operation(Mesh_operation_parameters&& context);
};

// Bridge edge loops (doc/plans/mesh_modeling.md section 4.10, catalog M14;
// erhe::geometry::operation::bridge_loops()) on the component selection
// snapshot: face mode deletes the selected faces and bridges their region
// boundaries; edge and vertex mode bridge the loops of the selected edges
// (vertex mode: the edges between selected vertices). The bridge faces are
// selected afterwards. An invalid selection (a vertex with three loop edges,
// fewer than two loops, ...) emits the primitive unchanged (logged).
class Bridge_loops_operation : public Mesh_operation
{
public:
    Bridge_loops_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Bridge_loops_options options);
};

// Flip (doc/plans/mesh_modeling.md catalog M10;
// erhe::geometry::operation::flip_facets()) on the component selection
// snapshot: reverses the winding of the facets of get_selection_facets()
// (the selected faces, or the faces whose vertices / edges are all
// selected). The selection is kept.
class Flip_facets_operation : public Mesh_operation
{
public:
    explicit Flip_facets_operation(Mesh_operation_parameters&& context);
};

// Recalculate normals outside / inside (catalog M10;
// erhe::geometry::operation::recalculate_facet_normals()). With a component
// selection snapshot it acts on the facets of get_selection_facets() (a
// primitive without a selection is emitted unchanged); without one (object
// mode) on every facet of each selected mesh. The selection is kept.
class Recalculate_normals_operation : public Mesh_operation
{
public:
    Recalculate_normals_operation(Mesh_operation_parameters&& context, erhe::geometry::operation::Normal_side side);
};

class Reverse_operation : public Mesh_operation
{
public:
    explicit Reverse_operation(Mesh_operation_parameters&& context);
};

class Normalize_operation : public Mesh_operation
{
public:
    explicit Normalize_operation(Mesh_operation_parameters&& context);
};

class Generate_tangents_operation : public Mesh_operation
{
public:
    explicit Generate_tangents_operation(Mesh_operation_parameters&& context);
};

class Generate_frame_field_tangents_operation : public Mesh_operation
{
public:
    Generate_frame_field_tangents_operation(Mesh_operation_parameters&& context, float sharp_angle_threshold);
};

class Make_raytrace_operation : public Mesh_operation
{
public:
    explicit Make_raytrace_operation(Mesh_operation_parameters&& context);
};

class Bake_transform_operation : public Mesh_operation
{
public:
    explicit Bake_transform_operation(Mesh_operation_parameters&& context);
};

class Repair_operation : public Mesh_operation
{
public:
    explicit Repair_operation(Mesh_operation_parameters&& context);
};

class Weld_operation : public Mesh_operation
{
public:
    explicit Weld_operation(Mesh_operation_parameters&& context);
};

class Remesh_operation : public Mesh_operation
{
public:
    Remesh_operation(Mesh_operation_parameters&& context, unsigned int target_point_count, bool regenerate_attributes);
};

class Anisotropic_remesh_operation : public Mesh_operation
{
public:
    Anisotropic_remesh_operation(Mesh_operation_parameters&& context, unsigned int target_point_count, float anisotropy, bool regenerate_attributes);
};

class Decimate_operation : public Mesh_operation
{
public:
    Decimate_operation(Mesh_operation_parameters&& context, unsigned int nb_bins, bool regenerate_attributes);
};

class Smooth_operation : public Mesh_operation
{
public:
    Smooth_operation(Mesh_operation_parameters&& context, unsigned int iterations, float strength, bool regenerate_attributes);
};

// Free-form deformation through a control point lattice (see
// erhe_geometry lattice_deform.hpp). With auto_fit_cage the cage box is
// fitted per mesh to the source geometry's local bounds (degenerate axes
// padded), so control point offsets deform relative to the mesh's own
// extent - the natural mode for one-shot script use (billowed sails,
// bent planks).
class Lattice_deform_operation : public Mesh_operation
{
public:
    Lattice_deform_operation(
        Mesh_operation_parameters&&                            context,
        erhe::geometry::operation::Lattice_deform_parameters&& lattice_parameters,
        bool                                                   auto_fit_cage
    );
};

// Overwrite corner texcoord channel 0 with a parametric projection (planar /
// cylindrical / spherical, see erhe_geometry project_texcoords.hpp) computed
// from each mesh's local bounds. Runs cleanly after deformations whose
// inherited parametrization is unusable (lattice-fanned fins).
class Project_texcoords_operation : public Mesh_operation
{
public:
    Project_texcoords_operation(
        Mesh_operation_parameters&&                            context,
        erhe::geometry::operation::Project_texcoords_parameters parameters
    );
};

class Make_atlas_operation : public Mesh_operation
{
public:
    // lightmap_texels_per_meter > 0 enables texel-density-aware chart
    // packing (see erhe_geometry make_atlas.hpp): the per-node world scale
    // folds in so gutters are sized for the lightmap region each instance
    // will get at that density.
    Make_atlas_operation(
        Mesh_operation_parameters&&                    context,
        std::size_t                                    usage_index,
        float                                          hard_angles_threshold,
        erhe::geometry::operation::Atlas_parameterizer parameterizer,
        erhe::geometry::operation::Atlas_packer        packer,
        float                                          lightmap_texels_per_meter = 0.0f,
        float                                          chart_gutter_texels       = 3.0f,
        float                                          chart_min_side_texels     = 2.0f,
        // Per-source-geometry per-facet chart order keys (per_facet
        // parameterizer only): pack similarly keyed facets next to each
        // other (Lightmap_baker::build_chart_order_keys provides baked
        // luminance for leak camouflage).
        std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>> per_facet_chart_order = {});
};

// CSG boolean over parameters.items in order: the FIRST mesh-carrying content
// node is the target (lhs), every following one is a tool (rhs). The result
// geometry - composed in the target node's local space - REPLACES the target
// mesh's primitives (node id, name, transform, children and rigid body
// all survive), and the tool nodes are removed (their children reparent up,
// like delete). Everything is one undoable compound operation.
class Binary_mesh_operation : public Compound_operation
{
public:
    Binary_mesh_operation(
        Mesh_operation_parameters&& parameters,
        const char*                 operation_name,
        std::function<void(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs,
            erhe::geometry::Geometry&       result
        )> operation
    );

protected:
    auto make_operations(
        Mesh_operation_parameters&& parameters,
        const char*                 operation_name,
        std::function<void(
            const erhe::geometry::Geometry& lhs,
            const erhe::geometry::Geometry& rhs,
            erhe::geometry::Geometry&       result
        )> operation
    ) -> Compound_operation::Parameters;
};

class Union_operation : public Binary_mesh_operation
{
public:
    explicit Union_operation(Mesh_operation_parameters&& parameters);
};

class Intersection_operation : public Binary_mesh_operation
{
public:
    explicit Intersection_operation(Mesh_operation_parameters&& parameters);
};

class Difference_operation : public Binary_mesh_operation
{
public:
    explicit Difference_operation(Mesh_operation_parameters&& parameters);
};

}
