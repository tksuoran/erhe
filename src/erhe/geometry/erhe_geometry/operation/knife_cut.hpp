#pragma once

#include "erhe_geometry/operation/edit_mesh_operation.hpp"

#include <geogram/basic/geometry.h>
#include <geogram/basic/numeric.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace erhe::geometry { class Geometry; }

namespace erhe::geometry::operation {

class Component_remap;

// Knife of doc/plans/mesh_modeling.md section 4.7 (M12), the geometric core:
// cut points in, cut edges out. Blender's knife is the behaviour reference.
//
// The view the cut is made from, in mesh-local space. clip_from_mesh maps a
// mesh-space point p to clip space as a column vector (clip_i = sum_j
// clip_from_mesh(i, j) * p_j, with p_3 = 1); the viewport maps normalized
// device x, y in [-1, 1] to [0, viewport_width] x [0, viewport_height]
// pixels. The cut plane of a segment contains its two points and
// eye_in_mesh (perspective) or view_direction_in_mesh (orthographic); the
// occlusion ray runs toward eye_in_mesh (perspective) or against
// view_direction_in_mesh (orthographic).
class Knife_view
{
public:
    GEO::mat4  clip_from_mesh;
    float      viewport_width        {1.0f};
    float      viewport_height       {1.0f};
    GEO::vec3f eye_in_mesh           {0.0f, 0.0f, 0.0f};
    GEO::vec3f view_direction_in_mesh{0.0f, 0.0f, -1.0f};
    bool       perspective           {true};
};

enum class Knife_snap : unsigned int
{
    vertex, // on the vertex `vertex` (position is taken from the mesh)
    edge,   // on the edge (edge_v0, edge_v1) (position projected onto it)
    facet   // inside the facet `facet` (position projected onto its plane)
};

// A cut point in mesh-local space. The members snap does not name stay
// GEO::NO_INDEX. Indices are source Geometry indices.
class Knife_point
{
public:
    GEO::vec3f   position{0.0f, 0.0f, 0.0f};
    Knife_snap   snap    {Knife_snap::facet};
    GEO::index_t vertex  {GEO::NO_INDEX};
    GEO::index_t edge_v0 {GEO::NO_INDEX};
    GEO::index_t edge_v1 {GEO::NO_INDEX};
    GEO::index_t facet   {GEO::NO_INDEX};
};

class Knife_options
{
public:
    bool  cut_through        {false}; // off: hits a ray toward the eye does not reach unoccluded are dropped
    bool  close_polyline     {false}; // finish() adds the segment from the last point back to the first
    float vertex_tolerance_px{0.5f};  // a vertex within this of the screen segment is a hit
    float edge_tolerance_px  {0.05f}; // an edge crossing within this of a segment end is that end
    float facet_tolerance_px {0.05f}; // two facet points closer than this on screen are one point
};

// Destination indices. cut_vertices are the vertices the cut created
// (ascending); cut_edges the destination edges along the cut, canonical
// (first < second) and ascending - what the knife tool selects afterwards.
class Knife_result
{
public:
    std::vector<GEO::index_t>                             cut_vertices;
    std::vector<std::pair<GEO::index_t, GEO::index_t>>    cut_edges;
};

// The knife as an incremental operation, driven point by point by the knife
// tool and finished on confirm.
//
// add_point(): the first point starts the polyline; every later point runs
// the segment-to-cuts step from the previous point. Hits are the two
// points; the vertices whose projection lies on the screen segment within
// vertex_tolerance_px (except near a segment end, which wins); and the edges
// of the facets crossing the cut plane whose crossing (the edge's
// intersection with the cut plane) projects onto the screen segment, not at
// an edge end and not within edge_tolerance_px of a segment end. Without
// cut_through a hit other than the two points is kept only when a ray from
// it toward the eye reaches the eye without crossing a facet that does not
// contain the hit element. Hits sort by the screen parameter along the
// segment, then by depth, and hits naming the same element merge. Per facet,
// the consecutive hits that belong to the facet (a vertex hit to the facets
// of the vertex, an edge hit to the facets of the edge, a point to its
// facet) become a cut edge of the facet unless they are the same cut vertex,
// lie on one original edge (two vertices joined by an edge, a vertex and a
// point on one of its edges, two points on one edge), or the midpoint of the
// two lies outside the facet (concave facets). A new cut edge crossing an
// earlier cut edge of the same facet splits both at a new facet point.
//
// Nothing touches the mesh before finish(): the cut vertices and cut edges
// are the knife's own record; undo_last_point() drops the last point and
// replays the others.
//
// finish(): splits each original edge at its cut vertices in parameter
// order, adds each facet point as a vertex (provenance: mean value
// coordinates of its facet's corners), then splits each cut facet along its
// cut edges with Edit_mesh::split_facet_edgenet() (a floating island is
// joined to the boundary by two connecting edges, a dangling edge is
// dropped). A facet point left without edges is deleted; an edge split
// vertex left without a cut edge is collapsed back into its edge. Then it
// emits. The remap maps a selected source facet to its pieces and a selected
// source edge to its split halves.
class Knife_cut : public Edit_mesh_operation
{
public:
    Knife_cut(const Geometry& source, Geometry& destination, const Knife_view& view, const Knife_options& options);

    // A point naming an element the source does not have is ignored (logged).
    void add_point      (const Knife_point& point);
    void undo_last_point();
    [[nodiscard]] auto get_point_count() const -> std::size_t;

    // The pending cut edges in mesh space (cleared and filled).
    void get_preview_segments(std::vector<std::pair<GEO::vec3f, GEO::vec3f>>& out_segments) const;

    // Applies the cut and emits into the destination. Call once. result and
    // remap are optional (remap needs both pointers set).
    void finish(Knife_result* result, Component_remap* remap);

private:
    // A cut vertex: an original vertex, a point on an original edge, or a
    // point inside an original facet.
    class Knife_vertex
    {
    public:
        GEO::vec3f   position      {0.0f, 0.0f, 0.0f};
        Knife_snap   kind          {Knife_snap::facet};
        GEO::index_t vertex        {GEO::NO_INDEX};
        GEO::index_t edge_v0       {GEO::NO_INDEX}; // edge: canonical, edge_v0 < edge_v1
        GEO::index_t edge_v1       {GEO::NO_INDEX};
        float        t             {0.0f};          // edge: parameter from edge_v0 to edge_v1
        GEO::index_t facet         {GEO::NO_INDEX};
        GEO::index_t scratch_vertex{GEO::NO_INDEX}; // set by finish()
    };

    class Knife_edge
    {
    public:
        std::size_t  a    {0}; // Knife_vertex indices
        std::size_t  b    {0};
        GEO::index_t facet{GEO::NO_INDEX};
    };

    class Knife_hit
    {
    public:
        Knife_vertex element;
        float        s       {0.0f}; // screen parameter along the segment
        float        depth   {0.0f};
        bool         endpoint{false};
        std::size_t  knife_vertex{0};
    };

    void process_point    (std::size_t point_index);
    void process_segment  (const Knife_point& from, const Knife_point& to);
    void rebuild          ();
    [[nodiscard]] auto make_element  (const Knife_point& point, Knife_vertex& out_element) -> bool;
    [[nodiscard]] auto project       (const GEO::vec3f& p, GEO::vec2f& out_pixel) const -> bool;
    [[nodiscard]] auto get_depth     (const GEO::vec3f& p) const -> float;
    [[nodiscard]] auto same_element  (const Knife_vertex& first, const Knife_vertex& second) const -> bool;
    [[nodiscard]] auto get_screen_distance(const GEO::vec3f& first, const GEO::vec3f& second) const -> float;
    [[nodiscard]] auto on_common_edge(const Knife_vertex& first, const Knife_vertex& second) const -> bool;
    [[nodiscard]] auto belongs_to    (const Knife_vertex& element, GEO::index_t facet) const -> bool;
    [[nodiscard]] auto is_occluded   (const Knife_vertex& element) -> bool;
    [[nodiscard]] auto contains_element(GEO::index_t facet, const Knife_vertex& element) const -> bool;
    auto find_or_add_knife_vertex(const Knife_vertex& element) -> std::size_t;
    void add_cut_edge            (std::size_t a, std::size_t b, GEO::index_t facet);
    void make_facet_frame        (GEO::index_t facet, GEO::vec3f& out_normal, GEO::vec3f& out_axis_u, GEO::vec3f& out_axis_v, GEO::vec3f& out_origin);
    [[nodiscard]] auto is_inside_facet(GEO::index_t facet, const GEO::vec3f& p) -> bool;

    Knife_view                m_view;
    Knife_options             m_options;
    std::vector<Knife_point>  m_points;
    std::vector<Knife_vertex> m_knife_vertices;
    std::vector<Knife_edge>   m_knife_edges;
    bool                      m_finished{false};

    // Scratch (cleared at point of use, capacity kept).
    std::vector<GEO::index_t>                          m_candidate_facets;
    std::vector<std::uint8_t>                          m_edge_marks;
    std::vector<std::uint8_t>                          m_vertex_marks;
    std::vector<Knife_hit>                             m_hits;
    std::vector<Knife_hit>                             m_merged_hits;
    std::vector<GEO::index_t>                          m_touched_facets;
    std::vector<std::size_t>                           m_facet_hits;
    std::vector<std::pair<std::size_t, std::size_t>>   m_work;
    std::vector<GEO::vec3f>                            m_polygon;
    std::vector<GEO::vec2f>                            m_polygon_2d;
    std::vector<float>                                 m_weights;
    std::vector<std::size_t>                           m_order;
    std::vector<std::pair<GEO::index_t, GEO::index_t>> m_net_edges;
    std::vector<GEO::index_t>                          m_out_facets;
};

// The numeric path: runs Knife_cut over the points (one polyline) and
// finishes it.
void knife_cut(
    const Geometry&              source,
    Geometry&                    destination,
    const Knife_view&            view,
    std::span<const Knife_point> points,
    const Knife_options&         options,
    Knife_result*                result = nullptr,
    Component_remap*             remap  = nullptr
);

} // namespace erhe::geometry::operation
