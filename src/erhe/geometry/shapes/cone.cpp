// #define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE

#include "erhe/geometry/shapes/cone.hpp"
#include "erhe/geometry/geometry_log.hpp"
#include "erhe/log/log_glm.hpp"
#include "erhe/toolkit/profile.hpp"
#include "erhe/toolkit/verify.hpp"

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

// Stack numbering example
//
//                      rel    known
// stack                stack  as
// --------------------------------------------------------------------------------.
// 6         /\         1.0    top       (singular = single vertex in this case)   .
// 5        /--\                                                                   .
// 4       /----\                                                                  .
// 3      /------\                                                                 .
// 2     /--------\                                                                .
// 1    /----------\                                                               .
// 0   /============\   0.0    bottom    (not singular in this case)               .
//
// bottom: stack == 0
// top:    stack == stack_count

class Conical_frustum_builder
{
public:
    Geometry& geometry;

    double min_x         {0.0};
    double max_x         {0.0};
    double bottom_radius {0.0};
    double top_radius    {0.0};
    bool   use_bottom    {false};
    bool   use_top       {false};
    int    slice_count   {0};
    int    stack_count   {0};
    bool   bottom_singular{false};
    bool   top_singular   {false};

    std::map<std::pair<int, int>, Point_id> points;

    Point_id top_point_id   {0};
    Point_id bottom_point_id{0};

    Property_map<Point_id  , vec3>* point_locations  {nullptr};
    Property_map<Point_id  , vec3>* point_normals    {nullptr};
    Property_map<Point_id  , vec4>* point_tangents   {nullptr};
    Property_map<Point_id  , vec4>* point_bitangents {nullptr};
    Property_map<Point_id  , vec2>* point_texcoords  {nullptr};
    Property_map<Corner_id , vec3>* corner_normals   {nullptr};
    Property_map<Corner_id , vec4>* corner_tangents  {nullptr};
    Property_map<Corner_id , vec4>* corner_bitangents{nullptr};
    Property_map<Corner_id , vec2>* corner_texcoords {nullptr};
    Property_map<Corner_id , vec4>* corner_colors    {nullptr};
    Property_map<Polygon_id, vec3>* polygon_centroids{nullptr};
    Property_map<Polygon_id, vec3>* polygon_normals  {nullptr};
    Property_map<Polygon_id, vec4>* polygon_colors   {nullptr};

    auto get_point(const int slice, const int stack) -> Point_id
    {
        return points[std::make_pair(slice == slice_count ? 0 : slice, stack)];
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

    auto make_point_data(const double rel_slice, const double rel_stack) const -> Point_data
    {
        ERHE_PROFILE_FUNCTION();

        const double phi                 = glm::pi<double>() * 2.0 * rel_slice;
        const double sin_phi             = std::sin(phi);
        const double cos_phi             = std::cos(phi);
        const double one_minus_rel_stack = 1.0 - rel_stack;

        const vec3 position{
            static_cast<float>(one_minus_rel_stack * (min_x                  ) + rel_stack * (max_x)),
            static_cast<float>(one_minus_rel_stack * (bottom_radius * sin_phi) + rel_stack * (top_radius * sin_phi)),
            static_cast<float>(one_minus_rel_stack * (bottom_radius * cos_phi) + rel_stack * (top_radius * cos_phi))
        };

        const vec3 bottom{
            static_cast<float>(min_x),
            static_cast<float>(bottom_radius * sin_phi),
            static_cast<float>(bottom_radius * cos_phi)
        };

        const vec3 top{
            static_cast<float>(max_x),
            static_cast<float>(top_radius * sin_phi),
            static_cast<float>(top_radius * cos_phi)
        };

        const vec3 B = glm::normalize(top - bottom); // generatrix
        const vec3 T{
            0.0f,
            static_cast<float>(std::sin(phi + glm::half_pi<double>())),
            static_cast<float>(std::cos(phi + glm::half_pi<double>()))
        };
        const vec3 N0 = glm::cross(B, T);
        const vec3 N  = glm::normalize(N0);

        const vec3  t_xyz = glm::normalize(T - N * glm::dot(N, T));
        const float t_w   = (glm::dot(glm::cross(N, T), B) < 0.0f) ? -1.0f : 1.0f;
        const vec3  b_xyz = glm::normalize(B - N * glm::dot(N, B));
        const float b_w   = (glm::dot(glm::cross(B, N), T) < 0.0f) ? -1.0f : 1.0f;

        const double s = rel_slice;
        const double t = rel_stack;

        return Point_data{
            .position  = position,
            .normal    = N,
            .tangent   = vec4{t_xyz, t_w},
            .bitangent = vec4{b_xyz, b_w},
            .texcoord  = vec2{static_cast<float>(s), static_cast<float>(t)}
        };
    }

    [[nodiscard]] auto cone_point(
        const double rel_slice,
        const double rel_stack
    ) -> Point_id
    {
        ERHE_PROFILE_FUNCTION();

        const Point_id point_id = geometry.make_point();

        const auto data = make_point_data(rel_slice, rel_stack);

        point_locations ->put(point_id, data.position);
        point_normals   ->put(point_id, data.normal);
        point_tangents  ->put(point_id, data.tangent);
        point_bitangents->put(point_id, data.bitangent);
        point_texcoords ->put(point_id, data.texcoord);
        SPDLOG_LOGGER_TRACE(
            log_cone,
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

    auto make_corner(
        const Polygon_id polygon,
        const int        slice,
        const int        stack
    ) -> Corner_id
    {
        return make_corner(polygon, slice, stack, false);
    }

    auto get_point_id(
        const int slice,
        const int stack
    )
    {
        ERHE_PROFILE_FUNCTION();

        if ((stack == 0) && bottom_singular) {
            return bottom_point_id;
        } else if ((stack == stack_count) && top_singular) {
            return top_point_id;
        } else if (slice == slice_count) {
            return points[std::make_pair(0, stack)];
        } else {
            return points[std::make_pair(slice, stack)];
        }
    }

    auto make_corner(
        const Polygon_id polygon_id,
        const int        slice,
        const int        stack,
        const bool       base
    ) -> Corner_id
    {
        ERHE_PROFILE_FUNCTION();

        const auto rel_slice = static_cast<double>(slice) / static_cast<double>(slice_count);
        const auto rel_stack = static_cast<double>(stack) / static_cast<double>(stack_count);

        const bool is_slice_seam       = (slice == 0) || (slice == slice_count);
        const bool is_bottom           = (stack == 0);
        const bool is_top              = (stack == stack_count);
        const bool is_uv_discontinuity = is_slice_seam || is_bottom || is_top;

        Point_id point_id{0};

#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        std::stringstream ss;

        ss << fmt::format(
            "polygon_id = {:2}, slice = {: 3}, stack = {: 3}, rel_slice = {: 3.1}, rel_stack = {: 3.1}, "
            "base = {:>5}, point_id = ",
            polygon_id,
            slice,
            stack,
            rel_slice,
            rel_stack,
            base
        );
#endif
        if (is_top && (top_radius == 0.0)) {
            point_id = top_point_id;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("{:2} (top)        ", point_id);
#endif
        } else if (is_bottom && (bottom_radius == 0.0)) {
            point_id = bottom_point_id;
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("{:2} (bottom)     ", point_id);
#endif
        } else if (slice == slice_count) {
            point_id = points[std::make_pair(0, stack)];
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("{:2} (slice seam) ", point_id);
#endif
        } else {
            point_id = points[std::make_pair(slice, stack)];
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format("{:2}              ", point_id);
#endif
        }

        const Corner_id corner_id = geometry.make_polygon_corner(polygon_id, point_id);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
        ss << fmt::format("corner = {:2}", corner_id);
#endif

        if (is_uv_discontinuity) {
            float s, t;
            if (base) {
                const double phi                 = glm::pi<double>() * 2.0 * rel_slice;
                const double sin_phi             = std::sin(phi);
                const double cos_phi             = std::cos(phi);
                const double one_minus_rel_stack = 1.0 - rel_stack;

                s = static_cast<float>(one_minus_rel_stack * sin_phi + rel_stack * sin_phi);
                t = static_cast<float>(one_minus_rel_stack * cos_phi + rel_stack * cos_phi);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                ss << fmt::format(" base");
#endif
            } else {
                s = static_cast<float>(rel_slice);
                t = static_cast<float>(rel_stack);
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                ss << fmt::format(" slice seam rel_slice = {: 3.1} rel_stack = {: 3.1}", rel_slice, rel_stack);
#endif
            }
            corner_texcoords->put(corner_id, vec2{s, t});
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
            ss << fmt::format(" UV discontinuity, texcoord = {: 3.1}, {: 3.1}", s, t);
#endif
        }

        if (is_top || is_bottom) {
            if (base && is_bottom && (bottom_radius != 0.0) && use_bottom) {
                corner_normals->put(corner_id, vec3{-1.0f, 0.0f, 0.0f});
                corner_tangents->put(
                    corner_id,
                    bottom_singular
                        ? vec4{0.0f, 0.0f, 0.0f, 0.0f}
                        : vec4{0.0f, 1.0f, 0.0f, 1.0f}
                );
                corner_bitangents->put(
                    corner_id,
                    bottom_singular
                        ? vec4{0.0f, 0.0f, 0.0f, 0.0f}
                        : vec4{0.0f, 0.0f, 1.0f, 1.0f}
                );
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                ss << fmt::format(" forced bottom normal and tangent");
#endif
            }

            if (base && is_top && (top_radius != 0.0) && use_top) {
                corner_normals->put(corner_id, vec3{1.0f, 0.0f, 0.0f});
                corner_tangents->put(
                    corner_id,
                    bottom_singular
                        ? vec4{0.0f, 0.0f, 0.0f, 0.0f}
                        : vec4{0.0f, 1.0f, 0.0f, 1.0f}
                );
                corner_bitangents->put(
                    corner_id,
                    bottom_singular
                        ? vec4{0.0f, 0.0f, 0.0f, 0.0f}
                        : vec4{0.0f, 0.0f, 1.0f, 1.0f}
                );
#if SPDLOG_ACTIVE_LEVEL <= SPDLOG_LEVEL_TRACE
                ss << fmt::format(" forced top normal and tangent");
#endif
            }
        }
        SPDLOG_LOGGER_TRACE(log_cone, ss.str());

        return corner_id;
    }

    Conical_frustum_builder(
        Geometry&    geometry,
        const double min_x,
        const double max_x,
        const double bottom_radius,
        const double top_radius,
        const bool   use_bottom,
        const bool   use_top,
        const int    slice_count,
        const int    stack_count
    )
        : geometry       {geometry}
        , min_x          {min_x}
        , max_x          {max_x}
        , bottom_radius  {bottom_radius}
        , top_radius     {top_radius}
        , use_bottom     {use_bottom}
        , use_top        {use_top}
        , slice_count    {slice_count}
        , stack_count    {stack_count}
        , bottom_singular{bottom_radius == 0.0}
        , top_singular   {top_radius    == 0.0}
    {
        ERHE_PROFILE_FUNCTION();

        ERHE_VERIFY(slice_count != 0);
        ERHE_VERIFY(stack_count != 0);
        point_locations   = geometry.point_attributes  ().create<vec3>(c_point_locations  );
        point_normals     = geometry.point_attributes  ().create<vec3>(c_point_normals    );
        point_tangents    = geometry.point_attributes  ().create<vec4>(c_point_tangents   );
        point_bitangents  = geometry.point_attributes  ().create<vec4>(c_point_bitangents );
        point_texcoords   = geometry.point_attributes  ().create<vec2>(c_point_texcoords  );
        corner_normals    = geometry.corner_attributes ().create<vec3>(c_corner_normals   );
        corner_tangents   = geometry.corner_attributes ().create<vec4>(c_corner_tangents  );
        corner_bitangents = geometry.corner_attributes ().create<vec4>(c_corner_bitangents);
        corner_texcoords  = geometry.corner_attributes ().create<vec2>(c_corner_texcoords );
        corner_colors     = geometry.corner_attributes ().create<vec4>(c_corner_colors    );
        polygon_centroids = geometry.polygon_attributes().create<vec3>(c_polygon_centroids);
        polygon_normals   = geometry.polygon_attributes().create<vec3>(c_polygon_normals  );
        polygon_colors    = geometry.polygon_attributes().create<vec4>(c_polygon_colors   );
    }

    void build()
    {
        ERHE_PROFILE_FUNCTION();

        // red channel   = anisotropy strength
        // green channel = apply texture coordinate to anisotropy
        const glm::vec4 non_anisotropic          {0.0f, 0.0f, 0.0f, 0.0f}; // Used on tips
        const glm::vec4 anisotropic_no_texcoord  {1.0f, 0.0f, 0.0f, 0.0f}; // Used on lateral surface
        const glm::vec4 anisotropic_with_texcoord{1.0f, 1.0f, 0.0f, 0.0f}; // Used on bottom / top ends

        // Points
        SPDLOG_LOGGER_TRACE(log_cone, "Points:");
        const int stack_bottom              = 0;
        const int stack_top                 = stack_count;
        const int stack_non_singular_bottom = bottom_singular ? 1               : 0;
        const int stack_non_singular_top    = top_singular    ? stack_count - 1 : stack_count;
        for (
            int stack = stack_non_singular_bottom;
            stack <= stack_non_singular_top;
            ++stack
        ) {
            const auto rel_stack = static_cast<double>(stack) / static_cast<double>(stack_count);
            for (int slice = 0; slice < slice_count; ++slice) {
                const auto rel_slice = static_cast<double>(slice) / static_cast<double>(slice_count);

                SPDLOG_LOGGER_TRACE(log_cone, "slice {:2} stack {: 2}: ", slice, stack);
                points[std::make_pair(slice, stack)] = cone_point(rel_slice, rel_stack);
            }
        }

        // Bottom parts
        if (bottom_singular) {
            SPDLOG_LOGGER_TRACE(log_cone, "Bottom - point / triangle fan");
            bottom_point_id = geometry.make_point(min_x, 0.0, 0.0); // Apex
            for (int slice = 0; slice < slice_count; ++slice) {
                const int  stack              = stack_non_singular_bottom;
                const auto rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                const auto rel_stack_centroid = (static_cast<double>(stack) - 0.5) / static_cast<double>(slice_count);

                const Polygon_id polygon_id   = geometry.make_polygon();
                const Point_data average_data = make_point_data(rel_slice_centroid, 0.0);

                make_corner(polygon_id, slice + 1, stack);
                make_corner(polygon_id, slice,     stack);
                const Corner_id tip_corner_id = make_corner(polygon_id, slice, stack_bottom);

                const auto p0 = get_point(slice,     stack);
                const auto p1 = get_point(slice + 1, stack);

                corner_normals   ->put(tip_corner_id, average_data.normal);
                corner_tangents  ->put(tip_corner_id, average_data.tangent);
                corner_bitangents->put(tip_corner_id, average_data.bitangent);
                corner_texcoords ->put(tip_corner_id, average_data.texcoord);
                corner_colors    ->put(tip_corner_id, non_anisotropic);

                const Point_data centroid_data = make_point_data(rel_slice_centroid, rel_stack_centroid);
                const auto flat_centroid_location = (1.0f / 3.0f) *
                    (
                        point_locations->get(p0) +
                        point_locations->get(p1) +
                        glm::vec3{min_x, 0.0, 0.0}
                    );
                polygon_centroids->put(polygon_id, flat_centroid_location);
                polygon_normals  ->put(polygon_id, centroid_data.normal);
                polygon_colors   ->put(polygon_id, anisotropic_no_texcoord);
            }
        } else {
            if (use_bottom) {
                SPDLOG_LOGGER_TRACE(log_cone, "Bottom - flat polygon");
                const Polygon_id polygon_id = geometry.make_polygon();

                polygon_centroids->put(polygon_id, vec3{static_cast<float>(min_x), 0.0f, 0.0f});
                polygon_normals  ->put(polygon_id, vec3{-1.0f, 0.0f, 0.0f});
                polygon_colors   ->put(polygon_id, anisotropic_with_texcoord);

                for (int slice = 0; slice < slice_count; ++slice) {
                    make_corner(polygon_id, slice, 0, true);
                }

                geometry.polygons[polygon_id].compute_planar_texture_coordinates(
                    polygon_id,
                    geometry,
                    *corner_texcoords,
                    *polygon_centroids,
                    *polygon_normals,
                    *point_locations,
                    false
                );
            } else {
                SPDLOG_LOGGER_TRACE(log_cone, "Bottom - none");
            }
        }

        // Middle quads, bottom up
        SPDLOG_LOGGER_TRACE(log_cone, "Middle quads, bottom up");
        for (
            int stack = stack_non_singular_bottom;
            stack < stack_non_singular_top;
            ++stack
        ) {
            const double rel_stack_centroid =
                (static_cast<double>(stack) + 0.5) / static_cast<double>(stack_count);

            for (int slice = 0; slice < slice_count; ++slice) {
                const auto rel_slice_centroid =
                    (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);

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
                polygon_centroids->put(polygon_id, flat_centroid_location);
                polygon_normals  ->put(polygon_id, centroid_data.normal);
                polygon_colors   ->put(polygon_id, anisotropic_no_texcoord);
            }
        }

        // Top parts
        if (top_singular) {
            SPDLOG_LOGGER_TRACE(log_cone, "Top - point / triangle fan");
            top_point_id = geometry.make_point(max_x, 0.0, 0.0); //  apex

            for (int slice = 0; slice < slice_count; ++slice) {
                const int  stack              = stack_non_singular_top;
                const auto rel_slice_centroid = (static_cast<double>(slice) + 0.5) / static_cast<double>(slice_count);
                const auto rel_stack_centroid = (static_cast<double>(stack) + 0.5) / static_cast<double>(stack_count);

                const Polygon_id polygon_id   = geometry.make_polygon();
                const Point_data average_data = make_point_data(rel_slice_centroid, 1.0);

                make_corner(polygon_id, slice + 1, stack);
                make_corner(polygon_id, slice,     stack);
                const Corner_id tip_corner_id = make_corner(polygon_id, slice, stack_top);

                SPDLOG_LOGGER_TRACE(log_cone, "polygon {} tip tangent {}", polygon_id, average_data.tangent);
                corner_normals   ->put(tip_corner_id, average_data.normal);
                corner_tangents  ->put(tip_corner_id, average_data.tangent);
                corner_bitangents->put(tip_corner_id, average_data.bitangent);
                corner_texcoords ->put(tip_corner_id, average_data.texcoord);
                corner_colors    ->put(tip_corner_id, non_anisotropic);

                const Point_data centroid_data = make_point_data(rel_slice_centroid, rel_stack_centroid);

                const auto p0 = get_point(slice,     stack);
                const auto p1 = get_point(slice + 1, stack);
                const vec3 position_p0  = point_locations->get(p0);
                const vec3 position_p1  = point_locations->get(p1);
                const vec3 position_tip = glm::vec3{max_x, 0.0, 0.0};

                const auto flat_centroid_location = (1.0f / 3.0f) *
                    (
                        position_p0 +
                        position_p1 +
                        position_tip
                    );
                SPDLOG_LOGGER_TRACE(
                    log_cone,
                    "p0 {}: {}, p1 {}: {}, tip {} {} - centroid: {} polygon {}",
                    p0, position_p0,
                    p1, position_p1,
                    top_point_id, position_tip,
                    flat_centroid_location,
                    polygon_id
                );

                polygon_centroids->put(polygon_id, flat_centroid_location);
                polygon_normals  ->put(polygon_id, centroid_data.normal);
                polygon_colors   ->put(polygon_id, anisotropic_no_texcoord);
            }
        } else {
            if (use_top) {
                SPDLOG_LOGGER_TRACE(log_cone, "Top - flat polygon");
                const Polygon_id polygon_id = geometry.make_polygon();

                polygon_centroids->put(polygon_id, vec3{static_cast<float>(max_x), 0.0f, 0.0f});
                polygon_normals  ->put(polygon_id, vec3{1.0f, 0.0f, 0.0f});
                polygon_colors   ->put(polygon_id, anisotropic_with_texcoord);

                for (int slice = 0; slice < slice_count; ++slice) {
                    const int reverse_slice = slice_count - 1 - slice;
                    make_corner(polygon_id, reverse_slice, stack_top, true);
                }

                geometry.polygons[polygon_id].compute_planar_texture_coordinates(
                    polygon_id,
                    geometry,
                    *corner_texcoords,
                    *polygon_centroids,
                    *polygon_normals,
                    *point_locations,
                    false
                );
            } else {
                SPDLOG_LOGGER_TRACE(log_cone, "Top - none");
            }
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

auto make_conical_frustum(
    const double min_x,
    const double max_x,
    const double bottom_radius,
    const double top_radius,
    const bool   use_bottom,
    const bool   use_top,
    const int    slice_count,
    const int    stack_division
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "conical frustum",
        [=](auto& geometry)
        {
            Conical_frustum_builder builder{
                geometry,
                min_x,
                max_x,
                bottom_radius,
                top_radius,
                use_bottom,
                use_top,
                slice_count,
                stack_division
            };
            builder.build();
        }
    };
}

auto make_cone(
    const double min_x,
    const double max_x,
    const double bottom_radius,
    const bool   use_bottom,
    const int    slice_count,
    const int    stack_division
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "cone",
        [=](auto& geometry)
        {
            Conical_frustum_builder builder{
                geometry,
                min_x,          // min x
                max_x,          // max x
                bottom_radius,  // bottom raidus
                0.0,            // top radius
                use_bottom,     // use_bottom
                false,          // use top
                slice_count,    // stack count
                stack_division  // stack division
            };
            builder.build();
        }
    };
}

auto make_cylinder(
    const double min_x,
    const double max_x,
    const double radius,
    const bool   use_bottom,
    const bool   use_top,
    const int    slice_count,
    const int    stack_division
) -> Geometry
{
    ERHE_PROFILE_FUNCTION();

    return Geometry{
        "cylinder",
        [=](auto& geometry)
        {
            Conical_frustum_builder builder{
                geometry,
                min_x,
                max_x,
                radius,
                radius,
                use_bottom,
                use_top,
                slice_count,
                stack_division
            };
            builder.build();
        }
    };
}

} // namespace erhe::geometry::shapes

