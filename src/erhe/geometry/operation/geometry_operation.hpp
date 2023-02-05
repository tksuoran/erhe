#pragma once

#include "erhe/geometry/types.hpp"

#include <set>
#include <vector>

namespace erhe::geometry
{
    class Geometry;
}

namespace erhe::geometry::operation
{

class Geometry_operation
{
public:
    Geometry_operation(
        Geometry& source,
        Geometry& destination
    )
        : source     {source}
        , destination{destination}
    {
    }

    static constexpr std::size_t s_grow_size = 4096;
    Geometry&                                              source;
    Geometry&                                              destination;
    std::vector<Point_id  >                                point_old_to_new;
    std::vector<Polygon_id>                                polygon_old_to_new;
    std::vector<Corner_id >                                corner_old_to_new;
    std::vector<Edge_id   >                                edge_old_to_new;
    std::vector<Point_id  >                                old_polygon_centroid_to_new_points;
    std::vector<std::vector<std::pair<float, Point_id  >>> new_point_sources;
    std::vector<std::vector<std::pair<float, Corner_id >>> new_point_corner_sources;
    std::vector<std::vector<std::pair<float, Corner_id >>> new_corner_sources;
    std::vector<std::vector<std::pair<float, Polygon_id>>> new_polygon_sources;
    std::vector<std::vector<std::pair<float, Edge_id   >>> new_edge_sources;

private:
    static constexpr std::size_t s_max_edge_point_slots = 300;
    std::vector<Point_id> m_old_edge_to_new_points;

public:
    void post_processing           ();
    void make_points_from_points   ();
    void make_polygon_centroids    ();
    void reserve_edge_to_new_points();

    [[nodiscard]] auto find_or_make_point_from_edge(
        const Point_id    a,
        const Point_id    b,
        const std::size_t count = 1
    ) -> Point_id;

    void make_edge_midpoints(
        const std::initializer_list<float> relative_positions = { 0.5f }
    );

    [[nodiscard]] auto get_edge_new_point(
        const Point_id a,
        const Point_id b,
        const Point_id split_position = 0,
        const Point_id split_count    = 1
    ) const -> Point_id;

    // Creates a new point to Destination from old point.
    // The new point is linked to the old point in Source.
    // Old point is set as source for the new point with specified weight.
    //
    // weight      Weight for old point as source
    // old_point   Old point used as source for the new point
    // return      The new point.
    auto make_new_point_from_point(
        const float    point_weight,
        const Point_id old_point
    ) -> Point_id;

    // Creates a new point to Destination from old point.
    // The new point is linked to the old point in Source.
    // Old point is set as source for the new point with weight 1.0.
    //
    // old_point   Old point used as source for the new point
    // returns     The new point.
    auto make_new_point_from_point(const Point_id old_point) -> Point_id;

    // Creates a new point to Destination from centroid of old polygon.
    // The new point is linked to the old polygon in Source.
    // Each corner of the old polygon is added as source for the new point with weight 1.0.
    auto make_new_point_from_polygon_centroid(const Polygon_id old_polygon) -> Point_id;

    void add_polygon_centroid(
        const Point_id   new_point,
        const float      polygon_weight,
        const Polygon_id old_polygon
    );

    void add_point_ring(
        const Point_id new_point,
        const float    point_weight,
        const Point_id old_point
    );

    auto make_new_polygon_from_polygon(const Polygon_id old_polygon) -> Polygon_id;

    auto make_new_corner_from_polygon_centroid(
        const Polygon_id new_polygon,
        const Polygon_id old_polygon
    ) -> Corner_id;

    auto make_new_corner_from_point(
        const Polygon_id new_polygon,
        const Point_id   new_point
    ) -> Corner_id;

    auto make_new_corner_from_corner(
        const Polygon_id new_polygon,
        const Corner_id  old_corner
    ) -> Corner_id;

    void add_polygon_corners(
        const Polygon_id new_polygon,
        const Polygon_id old_polygon
    );

    void add_point_source(
        const Point_id new_point,
        const float    point_weight,
        const Point_id old_point
    );

    void add_point_corner_source(
        const Point_id  new_point,
        const float     corner_weight,
        const Corner_id old_corner
    );

    void add_corner_source(
        const Corner_id new_corner,
        const float     corner_weight,
        const Corner_id old_corner
    );

    // Inherit point sources to corner
    void distribute_corner_sources(
        const Corner_id new_corner,
        const float     point_weight,
        const Point_id  new_point
    );

    void add_polygon_source(
        const Polygon_id new_polygon,
        const float      polygon_weight,
        const Polygon_id old_polygon
    );

    void add_edge_source(
        const Edge_id new_edge,
        const float   edge_weight,
        const Edge_id old_edge
    );

    void build_destination_edges_with_sourcing();

    void interpolate_all_property_maps();
};

} // namespace namespace geometry
