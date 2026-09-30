#pragma once

#include <geogram/basic/geometry.h>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;
class Geometry_component_selection;

// Merge operations of doc/plans/mesh_modeling.md section 4.4, composed on an
// Edit_mesh scratch (erhe_geometry/edit_mesh.hpp): every merge maps vertices
// to a survivor and runs Edit_mesh::weld_vertices(), so a facet left with
// fewer than three vertices goes, and each kept corner keeps the attributes
// of the original corner at its position.
//
// The merged vertices are the selection's vertices together with the
// endpoints of its edges and the vertices of its facets, so a selection of
// any component mode works. With a non-null remap (both pointers set) the
// source selection is carried to the result: the survivor is selected when
// any vertex of its cluster was.

enum class Merge_type : unsigned int
{
    at_center,   // the lowest selected vertex survives, moved to the mean of the selected vertices
    at_position, // the lowest selected vertex survives, moved to Merge_vertices_options::position
    at_first,    // the lowest selected vertex survives, position kept
    at_last,     // the highest selected vertex survives, position kept
    collapse     // per connected island of selected edges: the lowest vertex survives, moved to the island mean
};

class Merge_vertices_options
{
public:
    Merge_type type{Merge_type::at_center};

    // Target of Merge_type::at_position, in the mesh's local space.
    GEO::vec3f position{0.0f, 0.0f, 0.0f};

    // Off: each kept corner keeps its corner texture coordinates. On: at
    // center, at position and collapse write, per texcoord channel, the
    // midpoint of the per-component extent of the merged vertices' source
    // corners to every corner of the survivor (per island for collapse).
    // No effect for at_first / at_last.
    bool merge_uvs{false};
};

// At center, at position and collapse average the vertex attributes over the
// merged cluster (the survivor's provenance is the cluster, equal weights);
// at first / at last keep the survivor's attributes. The selection has no
// history order, so "first" and "last" are the lowest and highest selected
// vertex index. Collapse islands are the connected components of the
// selected edges: the selection's edges, the edges of its facets, and every
// edge between two of its vertices.
void merge_vertices(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection& selection,
    Merge_vertices_options              options = {},
    Component_remap*                    remap   = nullptr
);

class Merge_by_distance_options
{
public:
    // Vertices closer than this (strictly) are merged.
    float threshold{1e-4f};

    // Move the survivor to the mean of its cluster and average the vertex
    // attributes over the cluster; off keeps the survivor's position and
    // attributes.
    bool use_centroid{true};

    // An unselected vertex within the threshold of a selected one joins its
    // cluster and is the survivor (it keeps its position); unselected
    // vertices never merge with each other alone.
    bool include_unselected{false};
};

// Clusters the selected vertices (every vertex when selection is nullptr)
// with an octree radius search: a cluster is a connected component of the
// "closer than threshold" relation. A two-vertex cluster keeps the lower
// index, a larger cluster the vertex nearest its centroid (ties: the lower
// index); with include_unselected, the survivor is chosen by the same rule
// among the cluster's unselected vertices when it has any.
void merge_by_distance(
    const Geometry&                     source,
    Geometry&                           destination,
    const Geometry_component_selection* selection,
    Merge_by_distance_options           options = {},
    Component_remap*                    remap   = nullptr
);

} // namespace erhe::geometry::operation
