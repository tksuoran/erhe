#include "erhe_geometry/shapes/disc.hpp"
#include "erhe_profile/profile.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <map>
#include <vector>

namespace erhe::geometry::shapes {

using glm::vec2;
using glm::vec3;

class Disc_builder
{
public:
    Geometry& geometry;

    double inner_radius;
    double outer_radius;
    int    slice_count;
    int    stack_count;
    int    slice_begin;
    int    slice_end;
    int    stack_begin;
    int    stack_end;
 
    std::map<std::pair<int, int>, Point_id> points;
    Point_id                                center_point_id{0};

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Corner_id , vec3>* corner_normals   {nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};

    auto get_point(const int slice, const int stack) -> Point_id
    {
        return points[std::make_pair(slice, stack)];
    }

    // rel_stack is in range 0..1
    auto make_point(const double rel_slice, const double rel_stack) -> Point_id
    {
        const double phi                 = glm::pi<double>() * 2.0 * rel_slice;
        const double sin_phi             = std::sin(phi);
        const double cos_phi             = std::cos(phi);
        const double one_minus_rel_stack = 1.0 - rel_stack;

        const vec3 position{
            static_cast<float>(one_minus_rel_stack * (outer_radius * cos_phi) + rel_stack * (inner_radius * cos_phi)),
            static_cast<float>(one_minus_rel_stack * (outer_radius * sin_phi) + rel_stack * (inner_radius * sin_phi)),
            0.0f
        };

        const auto s = static_cast<float>(rel_slice);
        const auto t = static_cast<float>(rel_stack);

        const Point_id point_id = geometry.make_point();
        point_locations->put(point_id, vec3{position.x, position.y, position.z});
        point_normals  ->put(point_id, vec3{0.0f, 0.0f, 1.0f});
        point_texcoords->put(point_id, vec2{s, t});

        return point_id;
    }

    auto make_corner(const Polygon_id polygon_id, const int slice, const int stack) -> Corner_id
    {
        const double rel_slice           = static_cast<double>(slice) / static_cast<double>(slice_count);
        const double rel_stack           = (stack_count == 1) ? 1.0 : static_cast<double>(stack) / (static_cast<double>(stack_count) - 1);
        const bool   is_slice_seam       = (slice == 0) || (slice == slice_count);
        const bool   is_center           = (stack == 0) && (inner_radius == 0.0);
        const bool   is_uv_discontinuity = is_slice_seam || is_center;

        Point_id point_id;
        if (is_center) {
            point_id = center_point_id;
        } else {
            if (slice == slice_count) {
                point_id = points[std::make_pair(0, stack)];
            } else {
                point_id = points[std::make_pair(slice, stack)];
            }
        }

        const Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);

        if (is_uv_discontinuity) {
            auto s = static_cast<float>(rel_slice);
            auto t = static_cast<float>(rel_stack);

            corner_texcoords->put(corner_id, vec2{s, t});
        }

        return corner_id;
    }

    Disc_builder(
        Geometry&    geometry,
        const double inner_radius,
        const double outer_radius,
        const int    slice_count,
        const int    stack_count,
        const int    slice_begin,
        const int    slice_end,
        const int    stack_begin,
        const int    stack_end
    )
        : geometry    {geometry}
        , inner_radius{inner_radius}
        , outer_radius{outer_radius}
        , slice_count {slice_count}
        , stack_count {stack_count}
        , slice_begin {slice_begin}
        , slice_end   {slice_end}
        , stack_begin {stack_begin}
        , stack_end   {stack_end}
    {
        point_locations   = geometry.point_attributes()  .create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes()  .create<vec3>(c_point_normals    );
        point_texcoords   = geometry.point_attributes()  .create<vec2>(c_point_texcoords  );
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
        corner_normals    = geometry.corner_attributes() .create<vec3>(c_corner_normals   );
        corner_texcoords  = geometry.corner_attributes() .create<vec2>(c_corner_texcoords );
    }

    void build()
    {
        // Make points
        for (int stack = 0; stack < stack_count; ++stack) {
            const double rel_stack =
                (stack_count == 1)
                    ? 1.0
                    : static_cast<double>(stack) / (static_cast<double>(stack_count) - 1);
            for (int slice = 0; slice <= slice_count; ++slice) {
                const double rel_slice = static_cast<double>(slice) / static_cast<double>(slice_count);

                points[std::make_pair(slice, stack)] = make_point(rel_slice, rel_stack);
            }
        }

        // Special case without center point
        if (stack_count == 1) {
            const Polygon_id polygon_id = geometry.make_polygon();
            polygon_centroids->put(polygon_id, vec3{0.0f, 0.0f, 0.0f});
            polygon_normals->put(polygon_id, vec3{0.0f, 0.0f, 1.0f});

            for (int slice = 0; slice < slice_count; ++slice) {
                make_corner(polygon_id, slice, 0);
            }
            return;
        }

        // Make center point if needed
        if (inner_radius == 0.0f) {
            center_point_id = geometry.make_point(0.0f, 0.0f, 0.0f);
        }

        // Quads/triangles
        for (int stack = stack_begin; stack < stack_end - 1; ++stack) {
            const double rel_stack_centroid = (stack_end == 1) ? 0.5 : static_cast<double>(stack) / (static_cast<double>(stack_count) - 1);

            for (int slice = slice_begin; slice < slice_end; ++slice) {
                const double     rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                const Point_id   centroid_id        = make_point(rel_slice_centroid, rel_stack_centroid);
                const Polygon_id polygon_id         = geometry.make_polygon();

                polygon_centroids->put(polygon_id, point_locations->get(centroid_id));
                polygon_normals->put(polygon_id, vec3{0.0f, 0.0f, 1.0f});
                if ((stack == 0) && (inner_radius == 0.0)) {
                    make_corner(polygon_id, slice, stack + 1);
                    make_corner(polygon_id, slice + 1, stack + 1);
                    const Corner_id tip_corner_id = make_corner(polygon_id, slice, stack);

                    const vec2 t1               = point_texcoords->get(get_point(slice, stack));
                    const vec2 t2               = point_texcoords->get(get_point(slice + 1, stack));
                    const vec2 average_texcoord = (t1 + t2) / 2.0f;
                    corner_texcoords->put(tip_corner_id, average_texcoord);
                } else {
                    make_corner(polygon_id, slice + 1, stack);
                    make_corner(polygon_id, slice, stack);
                    make_corner(polygon_id, slice, stack + 1);
                    make_corner(polygon_id, slice + 1, stack + 1);
                }
            }
        }

        geometry.make_point_corners();
        geometry.build_edges();
    }
};

auto make_disc(const double outer_radius, const double inner_radius, const int slice_count, const int stack_count) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "disc",
        [=](auto& geometry) {
            Disc_builder builder{geometry, outer_radius, inner_radius, slice_count, stack_count, 0, slice_count, 0, stack_count};
            builder.build();
        }
    };
}

auto make_disc(
    const double outer_radius,
    const double inner_radius,
    const int    slice_count,
    const int    stack_count,
    const int    slice_begin,
    const int    slice_end,
    const int    stack_begin,
    const int    stack_end
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "disc",
        [=](auto& geometry) {
            Disc_builder builder{geometry, outer_radius, inner_radius, slice_count, stack_count, slice_begin, slice_end, stack_begin, stack_end};
            builder.build();
        }
    };
}

} // namespace erhe::geometry::shapes
