#pragma once

#include "erhe/geometry/operation/geometry_operation.hpp"
#include <unordered_map>

namespace erhe::geometry::operation
{
class Catmull_clark_subdivision
    : public Geometry_operation
{
public:
    struct Edge_key
    {
        Edge_key(Point_id a, Point_id b)
            : a(std::min(a, b))
            , b(std::max(a, b))
        {
            assert(this->a < this->b);
        }

        Point_id a;
        Point_id b;
    };

    struct Edge_key_compare
    {
        bool operator()(const Edge_key& lhs, const Edge_key& rhs) const
        {
            if (lhs.a < rhs.a)
            {
                return true;
            }
            if (lhs.a == rhs.a)
            {
                return lhs.b < rhs.b;
            }
            return false;
        }
    };

    Catmull_clark_subdivision(Geometry& src, Geometry& destination);

    auto make_new_point_from_edge(Edge_key src_edge)
    -> Point_id;

    auto make_new_corner_from_edge_point(Polygon_id new_polygon, Edge_key src_edge)
    -> Corner_id;

private:
    std::map<Edge_key, Point_id, Edge_key_compare> m_old_edge_to_new_edge_points;
};

auto catmull_clark_subdivision(erhe::geometry::Geometry& source) -> erhe::geometry::Geometry;

} // namespace erhe::geometry::operation
