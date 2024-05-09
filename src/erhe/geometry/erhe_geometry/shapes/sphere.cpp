// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe_geometry/shapes/sphere.hpp"
#include "erhe_geometry/geometry_log.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <map>
#include <sstream>
#include <vector>

namespace erhe::geometry::shapes
{

using glm::vec2;
using glm::vec3;
using glm::vec4;

namespace
{

class Sphere_builder
{
public:
    using scalar = float;

    Geometry&    geometry;
    const scalar radius;
    const int    slice_count;
    const int    stack_count;

    std::map<std::pair<int, int>, Point_id> points;
    Point_id                                top_point_id   {0};
    Point_id                                bottom_point_id{0};

    Property_map<Point_id  , vec3>* point_locations      {nullptr};
    Property_map<Point_id  , vec3>* point_normals        {nullptr};
    Property_map<Point_id  , vec4>* point_tangents       {nullptr};
    Property_map<Point_id  , vec4>* point_bitangents     {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords      {nullptr};
    Property_map<Corner_id , vec3>* corner_normals       {nullptr};
    Property_map<Corner_id , vec4>* corner_tangents      {nullptr};
    Property_map<Corner_id , vec4>* corner_bitangents    {nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords     {nullptr};
    Property_map<Corner_id , vec2>* corner_aniso_control {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids    {nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals      {nullptr};
    Property_map<Polygon_id, vec2>* polygon_aniso_control{nullptr};

    auto get_point(const int slice, const int stack) -> Point_id
    {
        return points[std::make_pair(slice, stack)];
    }

    class Point_data
    {
    public:
        glm::vec3 position {0.0f};
        glm::vec3 normal   {0.0f};
        glm::vec4 tangent  {0.0f};
        glm::vec4 bitangent{0.0f};
        glm::vec2 texcoord {0.0f};
    };

    auto make_point_data(const scalar rel_slice, const scalar rel_stack) const -> Point_data
    {
        const scalar phi       = glm::pi<scalar>() * scalar{2.0} * rel_slice;
        const scalar sin_phi   = std::sin(phi);
        const scalar cos_phi   = std::cos(phi);

        const scalar theta     = (glm::pi<scalar>() * rel_stack) - glm::half_pi<scalar>();
        const scalar sin_theta = std::sin(theta);
        const scalar cos_theta = std::cos(theta);

        //const scalar phi_t0     = phi + glm::half_pi<scalar>();
        //const scalar sin_phi_t0 = std::sin(phi_t0);
        //const scalar cos_phi_t0 = std::cos(phi_t0);

        //const scalar theta_b0     = theta + glm::half_pi<scalar>();
        //const scalar sin_theta_b0 = std::sin(theta_b0);
        //const scalar cos_theta_b0 = std::cos(theta_b0);

        //const scalar stack_radius    = cos_theta;
        //const scalar stack_radius_b0 = cos_theta_b0;

        const auto N_x = static_cast<float>(cos_theta * cos_phi);
        const auto N_y = static_cast<float>(sin_theta);
        const auto N_z = static_cast<float>(cos_theta * sin_phi);

        const auto T_x = static_cast<float>(-sin_phi);
        const auto T_y = static_cast<float>(0.0f);
        const auto T_z = static_cast<float>(cos_phi);

        const vec3 N{N_x, N_y, N_z};
        const vec3 T{T_x, T_y, T_z};
        const vec3 B = glm::normalize(glm::cross(N, T));

        const auto xP = static_cast<float>(radius * N_x);
        const auto yP = static_cast<float>(radius * N_y);
        const auto zP = static_cast<float>(radius * N_z);

        const auto s = static_cast<float>(rel_slice);
        const auto t = static_cast<float>(rel_stack);

        SPDLOG_LOGGER_TRACE(
            log_sphere,
            "rel_slice = {: 3.1}, rel_stack = {: 3.1}, "
            "position = {}, texcoord = {}, normal = {}, tangent = {}",
            static_cast<float>(rel_slice), static_cast<float>(rel_stack),
            vec3{xP, yP, zP},
            vec2{static_cast<float>(s), static_cast<float>(t)},
            N,
            T
        );

        return Point_data{
            .position  = vec3{xP, yP, zP},
            .normal    = N,
            .tangent   = vec4{T, 1.0f},
            .bitangent = vec4{B, 1.0f},
            .texcoord  = vec2{static_cast<float>(s), static_cast<float>(t)}
        };
    }

    auto sphere_point(const scalar rel_slice, const scalar rel_stack) -> Point_id
    {
        const Point_id   point_id = geometry.make_point();
        const Point_data data     = make_point_data(rel_slice, rel_stack);

        point_locations ->put(point_id, data.position);
        point_normals   ->put(point_id, data.normal);
        point_tangents  ->put(point_id, data.tangent);
        point_bitangents->put(point_id, data.bitangent);
        point_texcoords ->put(point_id, data.texcoord);

        SPDLOG_LOGGER_TRACE(
            log_sphere,
            "point_id = {:3}, rel_slice = {: 3.1}, rel_stack = {: 3.1}, "
            "position = {}, texcoord = {}, normal = {}, tangent = {}",
            point_id, static_cast<float>(rel_slice), static_cast<float>(rel_stack),
            data.position,
            data.texcoord,
            data.normal,
            data.tangent
        );

        return point_id;
    }

    auto get_point_id(int slice, int stack) -> Point_id
    {
        const bool is_bottom = (stack == 0);
        const bool is_top    = (stack == stack_count);

        if (is_top) {
            return top_point_id;
        } else if (is_bottom) {
            return bottom_point_id;
        } else if (slice == slice_count) {
            return points[std::make_pair(0, stack)];
        } else {
            return points[std::make_pair(slice, stack)];
        }
    }

    auto make_corner(
        const Polygon_id polygon_id,
        const int        slice,
        const int        stack
    ) -> Corner_id
    {
        ERHE_VERIFY(slice >= 0);

        const scalar rel_slice           = static_cast<scalar>(slice) / static_cast<scalar>(slice_count);
        const scalar rel_stack           = static_cast<scalar>(stack) / static_cast<scalar>(stack_count);
        const bool   is_slice_seam       = /*(slice == 0) ||*/ (slice == slice_count);
        const bool   is_bottom           = (stack == 0);
        const bool   is_top              = (stack == stack_count);
        const bool   is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        std::stringstream ss;
        ss << fmt::format(
            "polygon {:3} slice = {: 2} stack = {: 2} rel_slice = {: #08f} rel_stack = {: #08f} using ",
            polygon_id, slice, stack, rel_slice, rel_stack
        );
#endif

        Point_id point_id;
        if (is_top) {
            point_id = top_point_id;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("point id {:2} (top)", point_id);
#endif
        } else if (is_bottom) {
            point_id = bottom_point_id;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("point id {:2} (bottom)", point_id);
#endif
        } else if (slice == slice_count) {
            point_id = points[std::make_pair(0, stack)];
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("point id {:2} (slice seam)", point_id);
#endif
        } else {
            point_id = points[std::make_pair(slice, stack)];
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("point id {:2}", point_id);
#endif
        }

        const Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);
        SPDLOG_LOGGER_TRACE(log_sphere, "{} corner id = {:2}", ss.str(), corner_id);

        if (is_uv_discontinuity) {
            const auto s = rel_slice;
            const auto t = rel_stack;
            corner_texcoords->put(corner_id, vec2{s, t});
        }

        return corner_id;
    }

    Sphere_builder(
        Geometry&    geometry,
        const scalar radius,
        const int    slice_count,
        const int    stack_count
    )
        : geometry   {geometry}
        , radius     {radius}
        , slice_count{slice_count}
        , stack_count{stack_count}
    {
        point_locations       = geometry.point_attributes  ().create<vec3>(c_point_locations      );
        point_normals         = geometry.point_attributes  ().create<vec3>(c_point_normals        );
        point_tangents        = geometry.point_attributes  ().create<vec4>(c_point_tangents       );
        point_bitangents      = geometry.point_attributes  ().create<vec4>(c_point_bitangents     );
        point_texcoords       = geometry.point_attributes  ().create<vec2>(c_point_texcoords      );
        corner_normals        = geometry.corner_attributes ().create<vec3>(c_corner_normals       );
        corner_tangents       = geometry.corner_attributes ().create<vec4>(c_corner_tangents      );
        corner_bitangents     = geometry.corner_attributes ().create<vec4>(c_corner_bitangents    );
        corner_texcoords      = geometry.corner_attributes ().create<vec2>(c_corner_texcoords     );
        corner_aniso_control  = geometry.corner_attributes ().create<vec2>(c_corner_aniso_control );
        polygon_centroids     = geometry.polygon_attributes().create<vec3>(c_polygon_centroids    );
        polygon_normals       = geometry.polygon_attributes().create<vec3>(c_polygon_normals      );
        polygon_aniso_control = geometry.polygon_attributes().create<vec2>(c_polygon_aniso_control);
    }

    void build()
    {
        // X = anisotropy strength (0.0 .. 1.0)
        // Y = apply texture coordinate to anisotropy (0.0 .. 1.0)
        const glm::vec2 non_anisotropic        {0.0f, 0.0f}; // Used on tips
        const glm::vec2 anisotropic_no_texcoord{1.0f, 0.0f}; // Used on lateral surface

        for (int stack = 1; stack < stack_count; ++stack) {
            const scalar rel_stack = static_cast<scalar>(stack) / static_cast<scalar>(stack_count);
            for (int slice = 0; slice < slice_count; ++slice) {
                const scalar rel_slice = static_cast<scalar>(slice) / static_cast<scalar>(slice_count);
                points[std::make_pair(slice, stack)] = sphere_point(rel_slice, rel_stack);
            }
        }

        // Bottom fan
        SPDLOG_LOGGER_TRACE(log_sphere, "bottom fan");
        bottom_point_id = geometry.make_point(0.0f, static_cast<float>(-radius), 0.0f);
        point_normals->put(bottom_point_id, vec3{0.0f,-1.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_sphere, "bottom point id = {}", bottom_point_id);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int        stack              = 1;
            const scalar     rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);
            const scalar     rel_stack_centroid = (static_cast<scalar>(stack) - scalar{0.5}) / static_cast<scalar>(slice_count);
            const Polygon_id polygon_id         = geometry.make_polygon();
            const Point_data average_data       = make_point_data(rel_slice_centroid, 0.0);
            const Corner_id  tip_corner_id      = make_corner(polygon_id, slice, 0);
            make_corner(polygon_id, slice,     stack);
            make_corner(polygon_id, slice + 1, stack);

            const auto p0 = get_point(slice,     stack);
            const auto p1 = get_point(slice + 1, stack);

            corner_normals      ->put(tip_corner_id, average_data.normal);
            corner_tangents     ->put(tip_corner_id, average_data.tangent);
            corner_bitangents   ->put(tip_corner_id, average_data.bitangent);
            corner_texcoords    ->put(tip_corner_id, average_data.texcoord);
            corner_aniso_control->put(tip_corner_id, non_anisotropic);

            const Point_data centroid_data = make_point_data(rel_slice_centroid, rel_stack_centroid);
            const auto flat_centroid_location = (1.0f / 3.0f) *
                (
                    point_locations->get(p0) +
                    point_locations->get(p1) +
                    glm::vec3{0.0, -radius, 0.0}
                );
            polygon_centroids    ->put(polygon_id, flat_centroid_location);
            polygon_normals      ->put(polygon_id, centroid_data.normal);
            polygon_aniso_control->put(polygon_id, anisotropic_no_texcoord);
        }

        // Middle quads, bottom up
        SPDLOG_LOGGER_TRACE(log_sphere, "Middle quads, bottom up");
        for (
            int stack = 1;
            stack < (stack_count - 1);
            ++stack
        ) {
            const scalar rel_stack_centroid =
                (static_cast<scalar>(stack) + scalar{0.5}) / static_cast<scalar>(stack_count);

            for (int slice = 0; slice < slice_count; ++slice) {
                const scalar rel_slice_centroid =
                    (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);

                const Polygon_id polygon_id = geometry.make_polygon();
                make_corner(polygon_id, slice + 1, stack    );
                make_corner(polygon_id, slice,     stack    );
                make_corner(polygon_id, slice,     stack + 1);
                make_corner(polygon_id, slice + 1, stack + 1);

                const Point_data centroid_data = make_point_data(rel_slice_centroid, rel_stack_centroid);
                const auto flat_centroid_location = (1.0f / 4.0f) *
                    (
                        point_locations->get(get_point_id(slice + 1, stack    )) +
                        point_locations->get(get_point_id(slice,     stack    )) +
                        point_locations->get(get_point_id(slice,     stack + 1)) +
                        point_locations->get(get_point_id(slice + 1, stack + 1))
                    );
                polygon_centroids    ->put(polygon_id, flat_centroid_location);
                polygon_normals      ->put(polygon_id, centroid_data.normal);
                polygon_aniso_control->put(polygon_id, anisotropic_no_texcoord);
            }
        }

        // Top fan
        SPDLOG_LOGGER_TRACE(log_sphere, "top fan");
        top_point_id = geometry.make_point(0.0f, static_cast<float>(radius), 0.0f);
        point_normals ->put(top_point_id, vec3{0.0f, 1.0f, 0.0f});
        SPDLOG_LOGGER_TRACE(log_sphere, "top point id    = {}", top_point_id);
        for (int slice = 0; slice < slice_count; ++slice) {
            const int    stack              = stack_count - 1;
            const scalar rel_slice_centroid = (static_cast<scalar>(slice) + scalar{0.5}) / static_cast<scalar>(slice_count);
            const scalar rel_stack_centroid = (static_cast<scalar>(stack) + scalar{0.5}) / static_cast<scalar>(stack_count);

            const Polygon_id polygon_id   = geometry.make_polygon();
            SPDLOG_LOGGER_TRACE(log_sphere, "make average data rel_slice = {}, rel_stack = {}", rel_slice_centroid, 1.0);
            const Point_data average_data = make_point_data(rel_slice_centroid, 1.0);

            make_corner(polygon_id, slice + 1, stack);
            make_corner(polygon_id, slice,     stack);
            const Corner_id tip_corner_id = make_corner(polygon_id, slice, stack_count);

            SPDLOG_LOGGER_TRACE(log_sphere, "polygon {} tip tangent {}", polygon_id, average_data.tangent);
            corner_normals      ->put(tip_corner_id, average_data.normal);
            corner_tangents     ->put(tip_corner_id, average_data.tangent);
            corner_bitangents   ->put(tip_corner_id, average_data.bitangent);
            corner_texcoords    ->put(tip_corner_id, average_data.texcoord);
            corner_aniso_control->put(tip_corner_id, non_anisotropic);

            const Point_data centroid_data = make_point_data(rel_slice_centroid, rel_stack_centroid);

            const auto p0 = get_point(slice,     stack);
            const auto p1 = get_point(slice + 1, stack);
            const vec3 position_p0  = point_locations->get(p0);
            const vec3 position_p1  = point_locations->get(p1);
            const vec3 position_tip = glm::vec3{0.0f, radius, 0.0};

            const auto flat_centroid_location = (1.0f / 3.0f) *
                (
                    position_p0 +
                    position_p1 +
                    position_tip
                );
            SPDLOG_LOGGER_TRACE(
                log_sphere,
                "p0 {}: {}, p1 {}: {}, tip {} {} - centroid: {} polygon {}",
                p0, position_p0,
                p1, position_p1,
                top_point_id, position_tip,
                flat_centroid_location,
                polygon_id
            );

            polygon_centroids    ->put(polygon_id, flat_centroid_location);
            polygon_normals      ->put(polygon_id, centroid_data.normal);
            polygon_aniso_control->put(polygon_id, anisotropic_no_texcoord);
        }

        geometry.make_point_corners();
        geometry.build_edges();
        geometry.promise_has_polygon_normals();
        geometry.promise_has_polygon_centroids();
        geometry.promise_has_normals();
        geometry.promise_has_tangents();
        geometry.promise_has_bitangents();
        geometry.promise_has_texture_coordinates();
    }
};

} // namespace

auto make_sphere(
    const double       radius,
    const unsigned int slice_count,
    const unsigned int stack_division
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "sphere",
        [=](auto& geometry) {
            Sphere_builder builder{
                geometry,
                static_cast<Sphere_builder::scalar>(radius),
                static_cast<int>(slice_count),
                static_cast<int>(stack_division)
            };
            builder.build();
        }
    };
}

} // namespace erhe::geometry::shapes
