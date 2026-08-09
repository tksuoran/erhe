#pragma once

#include "operations/mesh_operation.hpp"
#include "operations/compound_operation.hpp"

#include "erhe_geometry/operation/lattice_deform.hpp"
#include "erhe_geometry/operation/make_atlas.hpp"

#include <unordered_map>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace editor {

class Catmull_clark_subdivision_operation : public Mesh_operation
{
public:
    explicit Catmull_clark_subdivision_operation(Mesh_operation_parameters&& context);
};

class Sqrt3_subdivision_operation : public Mesh_operation
{
public:
    explicit Sqrt3_subdivision_operation(Mesh_operation_parameters&& context);
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
// mesh's primitives (node id, name, transform, children and physics attachment
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
