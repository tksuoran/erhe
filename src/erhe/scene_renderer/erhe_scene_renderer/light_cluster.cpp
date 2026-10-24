#include "erhe_scene_renderer/light_cluster.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/light_snapshot.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace erhe::scene_renderer {

namespace {

// Relative widening of every light's range sphere and every cluster's
// bounds: a fragment whose fp32 cluster lookup rounds across a cluster
// boundary still finds every light that reaches it (C4).
constexpr double c_bound_widening = 1.0 / 1024.0;

// Widening of a spot light's cone, in cosine space: the binner's cone has
// the half angle acos(outer_cos - c_cone_cos_widening), where outer_cos is
// the fp32 cosine the light array holds, so a fragment whose fp32
// get_spot_attenuation() (normalize, dot: a few ulps of 1.0) rounds across
// the cone's edge still finds the light (C4).
constexpr double c_cone_cos_widening = 1.0 / 65536.0;

// The cone test's cluster sphere: an fp32 upper bound of sqrt(3), and the
// relative widening of the radius (1 + 2^-10, exact in fp32).
constexpr float c_sqrt3_upper          = 1.7320509f;
constexpr float c_cone_radius_widening = 1.0009765625f;

// The light array slot ranges of the four binned categories: spot
// shadow-mapped, spot unshadowed, point shadow-mapped, point unshadowed
// (Light_set slot order: type major, shadow-mapped first).
class Category_bounds
{
public:
    explicit Category_bounds(const Light_layer_partition& partition)
    {
        begin[0] = partition.per_type_shadow[0] + partition.per_type_nonshadow[0];
        begin[1] = begin[0] + partition.per_type_shadow   [1];
        begin[2] = begin[1] + partition.per_type_nonshadow[1];
        begin[3] = begin[2] + partition.per_type_shadow   [2];
        begin[4] = begin[3] + partition.per_type_nonshadow[2];
    }
    std::size_t begin[5];
};

[[nodiscard]] auto pack_halves(const std::size_t low, const std::size_t high) -> uint32_t
{
    ERHE_VERIFY((low <= 0xffffu) && (high <= 0xffffu));
    return static_cast<uint32_t>(low) | (static_cast<uint32_t>(high) << 16u);
}

void write_uint(const std::span<std::byte> out, const std::size_t uint_index, const uint32_t value)
{
    ERHE_VERIFY(((uint_index + 1) * sizeof(uint32_t)) <= out.size_bytes());
    std::memcpy(out.data() + (uint_index * sizeof(uint32_t)), &value, sizeof(uint32_t));
}

// The view-space point on the ray through normalized device position (x, y)
// at node-space z = vz, for clip_from_node C: the two linear equations
// clip.x = x clip.w and clip.y = y clip.w in (vx, vy).
[[nodiscard]] auto unproject_at_z(const glm::dmat4& C, const double x, const double y, const double vz) -> glm::dvec3
{
    const double a11 = C[0][0] - (x * C[0][3]);
    const double a12 = C[1][0] - (x * C[1][3]);
    const double b1  = -(((C[2][0] - (x * C[2][3])) * vz) + (C[3][0] - (x * C[3][3])));
    const double a21 = C[0][1] - (y * C[0][3]);
    const double a22 = C[1][1] - (y * C[1][3]);
    const double b2  = -(((C[2][1] - (y * C[2][3])) * vz) + (C[3][1] - (y * C[3][3])));
    const double det = (a11 * a22) - (a12 * a21);
    ERHE_VERIFY(det != 0.0);
    return glm::dvec3{((b1 * a22) - (a12 * b2)) / det, ((a11 * b2) - (b1 * a21)) / det, vz};
}

[[nodiscard]] auto get_light_position(const erhe::scene::Light_projection_transforms& transforms) -> glm::dvec3
{
    // As Light_buffer::update() writes position_and_inner_spot_cos.
    return glm::dvec3{glm::vec3{transforms.world_from_light_camera.get_matrix() * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}}};
}

// Open sides of a cluster's bounds (C2): the outer tiles reach past the
// frustum's sides, slice 0 reaches toward and behind the camera and sideways.
// light_cluster_bin.comp uses the same bits.
constexpr uint32_t c_open_min_x = 1u << 0u;
constexpr uint32_t c_open_max_x = 1u << 1u;
constexpr uint32_t c_open_min_y = 1u << 2u;
constexpr uint32_t c_open_max_y = 1u << 3u;
constexpr uint32_t c_open_max_z = 1u << 4u;

// write_inputs() header[2].y bits.
constexpr uint32_t c_flag_x_min_at_left   = 1u << 0u;
constexpr uint32_t c_flag_y_min_at_bottom = 1u << 1u;

// The fp32 tests below are the ones res/shaders/light_cluster_bin.comp runs,
// operation for operation (the shader declares every intermediate precise;
// this file is compiled without floating-point contraction), so the CPU and
// GPU lists agree bit for bit.

[[nodiscard]] auto get_open_sides(
    const uint32_t ix,
    const uint32_t iy,
    const uint32_t k,
    const uint32_t tiles_x,
    const uint32_t tiles_y,
    const uint32_t flags
) -> uint32_t
{
    const bool x_min_at_left   = ((flags & c_flag_x_min_at_left)   != 0u);
    const bool y_min_at_bottom = ((flags & c_flag_y_min_at_bottom) != 0u);
    uint32_t open = 0u;
    if (ix == 0u)             { open |= x_min_at_left   ? c_open_min_x : c_open_max_x; }
    if (ix == (tiles_x - 1u)) { open |= x_min_at_left   ? c_open_max_x : c_open_min_x; }
    if (iy == 0u)             { open |= y_min_at_bottom ? c_open_min_y : c_open_max_y; }
    if (iy == (tiles_y - 1u)) { open |= y_min_at_bottom ? c_open_max_y : c_open_min_y; }
    if (k == 0u) {
        // Slice 0 takes every fragment nearer than the near plane, behind
        // the camera included (clip w <= 0 has no meaningful tile, so the
        // lookup puts it in some slice 0 tile).
        open |= c_open_min_x | c_open_max_x | c_open_min_y | c_open_max_y | c_open_max_z;
    }
    return open;
}

// The point of [lo, hi] nearest to c, with the sides whose bit (min_bit,
// max_bit) is set in open unbounded.
[[nodiscard]] auto nearest_on_axis(const float c, const float lo, const float hi, const uint32_t open, const uint32_t min_bit, const uint32_t max_bit) -> float
{
    if ((open & min_bit) != 0u) {
        return ((open & max_bit) != 0u) ? c : std::min(c, hi);
    }
    if ((open & max_bit) != 0u) {
        return std::max(c, lo);
    }
    return std::min(std::max(c, lo), hi);
}

// Whether a light sphere (centre, radius; radius < 0 unbounded) can reach
// slice k spanning view depths [d0, d1].
[[nodiscard]] auto sphere_reaches_slice(const glm::vec4& light, const uint32_t k, const float d0, const float d1) -> bool
{
    const float depth = -light.z;
    const float radius = light.w;
    const float depth_far  = depth + radius;
    const float depth_near = depth - radius;
    if ((k > 0u) && (depth_far < d0)) {
        return false;
    }
    return !(depth_near > d1);
}

// Whether a light sphere touches a cluster's widened bounds.
[[nodiscard]] auto sphere_reaches_bounds(const glm::vec4& light, const glm::vec3& lo, const glm::vec3& hi, const uint32_t open) -> bool
{
    const float nx = nearest_on_axis(light.x, lo.x, hi.x, open, c_open_min_x, c_open_max_x);
    const float ny = nearest_on_axis(light.y, lo.y, hi.y, open, c_open_min_y, c_open_max_y);
    const float nz = nearest_on_axis(light.z, lo.z, hi.z, open, 0u,           c_open_max_z); // the far side is never open
    const float ox = light.x - nx;
    const float oy = light.y - ny;
    const float oz = light.z - nz;
    const float xx = ox * ox;
    const float yy = oy * oy;
    const float zz = oz * oz;
    const float xy = xx + yy;
    const float distance_squared = xy + zz;
    const float radius_squared   = light.w * light.w;
    return distance_squared <= radius_squared;
}

// Whether a spot light's widened cone can reach a closed cluster's widened
// bounds. light.xyz is the apex (the light centre), cone.xyz the emission
// axis times s = sin(a), cone.w = cot(a), a the widened half angle (the
// write_inputs() layout). The test takes the sphere around the bounds -
// centre b, radius r an upper bound of the half diagonal without a square
// root (min of the L1 norm and sqrt(3) times the largest half extent) - and
// V = b - apex, along = dot(V, axis), perp = |cross(V, axis)|. The signed
// distance of b from the cone's lateral surface line in the plane of the
// axis and b is cos(a) perp - sin(a) along; it is a lower bound of b's
// distance to the cone for every a in (0, pi), so the sphere misses the
// cone when it exceeds r. For a <= pi / 2 the cone also lies in front of
// the apex plane, so the sphere misses it when along < -r. Both tests are
// evaluated on squares (no square root, so the CPU and GPU agree bit for
// bit): with t = r + sin(a) along, cos(a) perp > t is
//   cot >= 0: t < 0, or (cot perp s)^2 > t^2
//   cot <  0: t < 0 and (cot perp s)^2 < t^2
// and along < -r is s along < 0 and (s along)^2 (1 + cot^2) > r^2
// (1 + cot^2 = 1 / s^2).
[[nodiscard]] auto cone_reaches_bounds(const glm::vec4& light, const glm::vec4& cone, const glm::vec3& lo, const glm::vec3& hi) -> bool
{
    const float ex = (hi.x - lo.x) * 0.5f;
    const float ey = (hi.y - lo.y) * 0.5f;
    const float ez = (hi.z - lo.z) * 0.5f;
    const float bx = lo.x + ex;
    const float by = lo.y + ey;
    const float bz = lo.z + ez;
    const float exy      = ex + ey;
    const float l1       = exy + ez;
    const float e_max    = std::max(ex, std::max(ey, ez));
    const float l_max    = e_max * c_sqrt3_upper;
    const float r_bound  = std::min(l1, l_max);
    const float r        = r_bound * c_cone_radius_widening;
    const float vx = bx - light.x;
    const float vy = by - light.y;
    const float vz = bz - light.z;
    // s along
    const float ax       = vx * cone.x;
    const float ay       = vy * cone.y;
    const float az       = vz * cone.z;
    const float axy      = ax + ay;
    const float along_s  = axy + az;
    // cross(V, s): s perp is its length
    const float cx0      = vy * cone.z;
    const float cx1      = vz * cone.y;
    const float cy0      = vz * cone.x;
    const float cy1      = vx * cone.z;
    const float cz0      = vx * cone.y;
    const float cz1      = vy * cone.x;
    const float cx       = cx0 - cx1;
    const float cy       = cy0 - cy1;
    const float cz       = cz0 - cz1;
    const float cxx      = cx * cx;
    const float cyy      = cy * cy;
    const float czz      = cz * cz;
    const float cxy      = cxx + cyy;
    const float perp_s_sq = cxy + czz;
    const float cot_sq   = cone.w * cone.w;
    const float lateral  = perp_s_sq * cot_sq; // (cos(a) perp)^2
    const float t        = r + along_s;
    const float tt       = t * t;
    if (cone.w >= 0.0f) {
        if (t < 0.0f) {
            return false;
        }
        if (lateral > tt) {
            return false;
        }
        if (along_s < 0.0f) {
            const float along_sq  = along_s * along_s;
            const float inv_sin_sq = 1.0f + cot_sq;
            const float back      = along_sq * inv_sin_sq;
            const float rr        = r * r;
            if (back > rr) {
                return false;
            }
        }
        return true;
    }
    return !((t < 0.0f) && (lateral < tt));
}

} // anonymous namespace

auto get_cluster_slot_end(const Light_layer_partition& partition) -> std::size_t
{
    const Category_bounds bounds{partition};
    return bounds.begin[4];
}

auto get_single_cluster_byte_count(const Light_layer_partition& partition) -> std::size_t
{
    const Category_bounds bounds{partition};
    const std::size_t     count = bounds.begin[4] - bounds.begin[0];
    return (c_light_cluster_header_uint_count + ((count + 1) / 2)) * sizeof(uint32_t);
}

void write_single_cluster(const Light_layer_partition& partition, const std::span<std::byte> out)
{
    const Category_bounds bounds{partition};
    write_uint(out, 0, static_cast<uint32_t>(c_light_cluster_header_uint_count));
    write_uint(out, 1, pack_halves(partition.per_type_shadow[1], partition.per_type_nonshadow[1]));
    write_uint(out, 2, pack_halves(partition.per_type_shadow[2], partition.per_type_nonshadow[2]));
    std::size_t index = 0;
    for (std::size_t slot = bounds.begin[0]; slot < bounds.begin[4]; slot += 2, ++index) {
        const std::size_t high = ((slot + 1) < bounds.begin[4]) ? (slot + 1) : 0;
        write_uint(out, c_light_cluster_header_uint_count + index, pack_halves(slot, high));
    }
}

void Light_cluster_binner::build_grid(
    const Light_projections&    light_projections,
    const Light_cluster_view&   view,
    const Light_cluster_config& config
)
{
    ERHE_VERIFY(view.camera != nullptr);
    ERHE_VERIFY(config.tile_size > 0);
    ERHE_VERIFY(config.slice_count > 0);
    const erhe::scene::Camera_snapshot& camera = *view.camera;

    const glm::mat4  clip_from_node  = camera.projection().clip_from_node_transform(view.viewport, view.reverse_depth, view.depth_range, view.conventions).get_matrix();
    const glm::mat4& world_from_node = camera.world_from_node();
    const glm::vec3  view_origin     = get_view_origin(world_from_node);
    const glm::dmat4 node_from_world = glm::inverse(glm::dmat4{world_from_node});
    // The bins compare node-space depths and positions with light ranges in
    // world units, and the fragment lookup takes view depth as a dot product
    // with the node z axis: both assume the camera's world_from_node carries
    // no scale (a rotation and a translation). A scaled camera node scales
    // every cluster bound against the ranges, so its lists are not
    // conservative; the grid does not support one.

    m_grid.clip_from_cluster_relative = glm::mat4{get_clip_from_view_relative(clip_from_node, world_from_node, view_origin)};
    m_grid.view_origin = glm::vec4{view_origin, 0.0f};
    // The node-space z of a point p_c relative to the camera position is the
    // z row of node_from_world's linear part dotted with p_c (the camera
    // position is the node origin); view depth is its negation.
    m_grid.view_axis = glm::vec4{
        static_cast<float>(node_from_world[0][2]),
        static_cast<float>(node_from_world[1][2]),
        static_cast<float>(node_from_world[2][2]),
        0.0f
    };

    const int width  = std::max(view.viewport.width,  1);
    const int height = std::max(view.viewport.height, 1);
    const uint32_t tiles_x = static_cast<uint32_t>((width  + config.tile_size - 1) / config.tile_size);
    const uint32_t tiles_y = static_cast<uint32_t>((height + config.tile_size - 1) / config.tile_size);

    m_capacity = static_cast<std::size_t>(std::clamp(config.capacity, 1, 0xffff));
    const Category_bounds bounds{light_projections.light_partition};
    ERHE_VERIFY(light_projections.light_projection_transforms.size() >= bounds.begin[4]);
    for (std::size_t i = 0; i < 5; ++i) {
        m_category_begin[i] = static_cast<uint32_t>(bounds.begin[i]);
    }

    // The light spheres in the cluster camera's node space (composed in
    // double, rounded once), and the slices: from the near plane to the
    // farthest view depth a binned light reaches.
    const double z_near = std::max(static_cast<double>(camera.projection().get_z_near()), 1.0e-3);
    double z_far = z_near;
    m_lights.clear();
    m_cones.clear();
    const glm::dmat3 node_from_world_linear{node_from_world};
    for (std::size_t slot = bounds.begin[0]; slot < bounds.begin[4]; ++slot) {
        const erhe::scene::Light_projection_transforms& transforms = light_projections.light_projection_transforms[slot];
        const erhe::scene::Light_snapshot&              light      = *transforms.light;
        const double     range  = static_cast<double>(light.get_range());
        const glm::dvec3 centre = glm::dvec3{node_from_world * glm::dvec4{get_light_position(transforms), 1.0}};
        if (range <= 0.0) {
            // No range: inverse square with no cutoff reaches every fragment.
            m_lights.push_back(glm::vec4{glm::vec3{centre}, -1.0f});
            m_cones.push_back(glm::vec4{0.0f});
            continue;
        }
        const double radius = range * (1.0 + c_bound_widening);
        m_lights.push_back(glm::vec4{glm::vec3{centre}, static_cast<float>(radius)});
        z_far = std::max(z_far, -centre.z + radius);

        // The spot cone (cone_reaches_bounds()): get_spot_attenuation() is
        // nonzero only where the fp32 cosine of the angle between the
        // emission axis and the fragment exceeds the light array's fp32
        // outer cosine. Zero for point lights and for a cone widened to
        // the whole sphere (never tested / never culls).
        glm::vec4 cone{0.0f};
        if (light.get_light_type() == erhe::scene::Light_type::spot) {
            // As Light_buffer::update() writes direction_and_outer_spot_cos.
            const float  outer_cos   = std::cos(light.get_outer_spot_angle() * 0.5f);
            const double widened_cos = static_cast<double>(outer_cos) - c_cone_cos_widening;
            if (widened_cos > -1.0) {
                const glm::dvec3 direction{glm::normalize(light.get_light_frame().direction)};
                const glm::dvec3 axis      = glm::normalize(node_from_world_linear * -direction);
                const double     sin_a     = std::sin(std::acos(widened_cos));
                cone = glm::vec4{glm::vec3{axis * sin_a}, static_cast<float>(widened_cos / sin_a)};
            }
        }
        m_cones.push_back(cone);
    }
    const uint32_t slice_count = (z_far > (z_near * (1.0 + c_bound_widening)))
        ? static_cast<uint32_t>(config.slice_count)
        : 1u;
    const double log_ratio = std::log2(z_far / z_near);
    const double scale     = (slice_count > 1) ? (static_cast<double>(slice_count) / log_ratio) : 0.0;
    const double bias      = -std::log2(z_near) * scale;
    m_grid.slices = glm::vec4{static_cast<float>(scale), static_cast<float>(bias), static_cast<float>(z_near), 0.0f};
    m_grid.counts = glm::uvec4{tiles_x, tiles_y, slice_count, 0u};

    // Slice k spans view depths [d_k, d_k+1]; slice 0 starts at the camera
    // (it takes the fragments nearer than the z_near plane), the last one ends
    // at z_far. The bounds widening scales with the slice's z_far depth.
    m_slices.resize(slice_count);
    double d0 = 0.0;
    for (uint32_t k = 0; k < slice_count; ++k) {
        const double d1 = ((k + 1) < slice_count)
            ? z_near * std::exp2((static_cast<double>(k + 1) / static_cast<double>(slice_count)) * log_ratio)
            : std::max(z_far, z_near);
        const double widening = c_bound_widening * std::max(d1, 1.0e-3);
        m_slices[k] = glm::vec4{static_cast<float>(d0), static_cast<float>(d1), static_cast<float>(widening), 0.0f};
        d0 = d1;
    }

    // Tile corner rays: corner (ix, iy) is at normalized device position
    // (-1 + 2 ix / tiles_x, -1 + 2 iy / tiles_y), the tile lookup's
    // boundaries.
    const glm::dmat4 C{clip_from_node};
    m_corners.resize(2 * (tiles_x + 1) * (tiles_y + 1));
    for (uint32_t iy = 0; iy <= tiles_y; ++iy) {
        const double y = -1.0 + ((2.0 * static_cast<double>(iy)) / static_cast<double>(tiles_y));
        for (uint32_t ix = 0; ix <= tiles_x; ++ix) {
            const double      x      = -1.0 + ((2.0 * static_cast<double>(ix)) / static_cast<double>(tiles_x));
            const glm::dvec3  at_0   = unproject_at_z(C, x, y,  0.0);
            const glm::dvec3  at_1   = unproject_at_z(C, x, y, -1.0);
            const std::size_t corner = (static_cast<std::size_t>(iy) * (tiles_x + 1)) + ix;
            m_corners[(2 * corner) + 0] = glm::vec4{glm::vec3{at_0}, 0.0f};
            m_corners[(2 * corner) + 1] = glm::vec4{glm::vec3{at_1 - at_0}, 0.0f};
        }
    }
    // Which view-space side the first tile column / row faces, at depth 1.
    const bool x_min_at_left   = unproject_at_z(C, -1.0, 0.0, -1.0).x < unproject_at_z(C, 1.0, 0.0, -1.0).x;
    const bool y_min_at_bottom = unproject_at_z(C, 0.0, -1.0, -1.0).y < unproject_at_z(C, 0.0, 1.0, -1.0).y;
    m_open_flags = (x_min_at_left ? c_flag_x_min_at_left : 0u) | (y_min_at_bottom ? c_flag_y_min_at_bottom : 0u);
}

void Light_cluster_binner::prepare(
    const Light_projections&    light_projections,
    const Light_cluster_view&   view,
    const Light_cluster_config& config
)
{
    ERHE_PROFILE_FUNCTION();
    build_grid(light_projections, view, config);
}

void Light_cluster_binner::bin(
    const Light_projections&    light_projections,
    const Light_cluster_view&   view,
    const Light_cluster_config& config
)
{
    prepare(light_projections, view, config);
    bin_prepared();
}

void Light_cluster_binner::bin_prepared()
{
    ERHE_PROFILE_FUNCTION();

    const uint32_t    tiles_x       = m_grid.counts.x;
    const uint32_t    tiles_y       = m_grid.counts.y;
    const uint32_t    slice_count   = m_grid.counts.z;
    const std::size_t cluster_count = m_grid.get_cluster_count();
    const uint32_t    first_slot    = m_category_begin[0];

    // Every cluster's widened bounds, as light_cluster_bin.comp computes
    // them per invocation.
    m_cluster_min .resize(cluster_count);
    m_cluster_max .resize(cluster_count);
    m_cluster_open.resize(cluster_count);
    for (uint32_t k = 0; k < slice_count; ++k) {
        const glm::vec4& slice = m_slices[k];
        for (uint32_t iy = 0; iy < tiles_y; ++iy) {
            for (uint32_t ix = 0; ix < tiles_x; ++ix) {
                const std::size_t corner_0 = (static_cast<std::size_t>(iy) * (tiles_x + 1)) + ix;
                glm::vec3 lo{0.0f};
                glm::vec3 hi{0.0f};
                for (uint32_t c = 0; c < 4; ++c) {
                    const std::size_t corner    = corner_0 + (((c & 2u) != 0u) ? (tiles_x + 1) : 0u) + (c & 1u);
                    const glm::vec3   origin    {m_corners[(2 * corner) + 0]};
                    const glm::vec3   direction {m_corners[(2 * corner) + 1]};
                    const glm::vec3   step_0    = slice.x * direction;
                    const glm::vec3   step_1    = slice.y * direction;
                    const glm::vec3   p0        = origin + step_0;
                    const glm::vec3   p1        = origin + step_1;
                    if (c == 0) {
                        lo = glm::min(p0, p1);
                        hi = glm::max(p0, p1);
                    } else {
                        lo = glm::min(lo, glm::min(p0, p1));
                        hi = glm::max(hi, glm::max(p0, p1));
                    }
                }
                const std::size_t cluster = (((static_cast<std::size_t>(k) * tiles_y) + iy) * tiles_x) + ix;
                m_cluster_min [cluster] = lo - glm::vec3{slice.z};
                m_cluster_max [cluster] = hi + glm::vec3{slice.z};
                m_cluster_open[cluster] = get_open_sides(ix, iy, k, tiles_x, tiles_y, m_open_flags);
            }
        }
    }

    m_hits.clear();
    for (std::size_t light_index = 0, light_end = m_lights.size(); light_index < light_end; ++light_index) {
        const glm::vec4& light   = m_lights[light_index];
        const glm::vec4& cone    = m_cones[light_index];
        const uint32_t   slot    = first_slot + static_cast<uint32_t>(light_index);
        const bool       is_spot = (slot < m_category_begin[2]);
        if (light.w < 0.0f) {
            for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
                m_hits.push_back(Hit{.cluster = static_cast<uint32_t>(cluster), .slot = slot});
            }
            continue;
        }
        for (uint32_t k = 0; k < slice_count; ++k) {
            if (!sphere_reaches_slice(light, k, m_slices[k].x, m_slices[k].y)) {
                continue;
            }
            const std::size_t slice_begin = static_cast<std::size_t>(k) * tiles_y * tiles_x;
            const std::size_t slice_end   = slice_begin + (static_cast<std::size_t>(tiles_y) * tiles_x);
            for (std::size_t cluster = slice_begin; cluster < slice_end; ++cluster) {
                // The cone test only for closed clusters (an open side has
                // no bounding sphere) and only for spot lights.
                const uint32_t open = m_cluster_open[cluster];
                if (
                    sphere_reaches_bounds(light, m_cluster_min[cluster], m_cluster_max[cluster], open) &&
                    (!is_spot || (open != 0u) || cone_reaches_bounds(light, cone, m_cluster_min[cluster], m_cluster_max[cluster]))
                ) {
                    m_hits.push_back(Hit{.cluster = static_cast<uint32_t>(cluster), .slot = slot});
                }
            }
        }
    }

    // Counting sort by cluster. The hits are in ascending slot order (the
    // walk above), and the scatter keeps that order per cluster.
    const std::size_t capacity = m_capacity;
    m_cluster_counts.assign(cluster_count, 0u);
    for (const Hit& hit : m_hits) {
        ++m_cluster_counts[hit.cluster];
    }
    m_overflow_cluster_count = 0;
    m_max_list_length        = 0;
    m_cluster_offsets.resize(cluster_count + 1);
    uint32_t running = 0;
    for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
        const uint32_t count = m_cluster_counts[cluster];
        m_max_list_length = std::max(m_max_list_length, static_cast<std::size_t>(count));
        if (count > capacity) {
            ++m_overflow_cluster_count;
        }
        m_cluster_offsets[cluster] = running;
        running += static_cast<uint32_t>(std::min(static_cast<std::size_t>(count), capacity));
        m_cluster_counts[cluster] = 0u; // reused as the scatter cursor
    }
    m_cluster_offsets[cluster_count] = running;
    m_sorted_slots.resize(running);
    m_category_counts.assign(cluster_count * 4, 0u);
    for (const Hit& hit : m_hits) {
        uint32_t& cursor = m_cluster_counts[hit.cluster];
        if (cursor >= capacity) {
            continue;
        }
        m_sorted_slots[m_cluster_offsets[hit.cluster] + cursor] = hit.slot;
        ++cursor;
        std::size_t category = 0;
        while (hit.slot >= m_category_begin[category + 1]) {
            ++category;
        }
        ++m_category_counts[(hit.cluster * 4) + category];
    }
}

auto Light_cluster_binner::get_input_byte_count() const -> std::size_t
{
    const std::size_t vec4_count = m_slices.size() + m_corners.size() + (2 * std::max(m_lights.size(), std::size_t{1}));
    return (c_light_cluster_bin_header_uvec4_count * sizeof(glm::uvec4)) + (vec4_count * sizeof(glm::vec4));
}

void Light_cluster_binner::write_inputs(const std::span<std::byte> out) const
{
    ERHE_VERIFY(out.size_bytes() >= get_input_byte_count());
    const std::size_t cluster_count = m_grid.get_cluster_count();
    const std::array<glm::uvec4, c_light_cluster_bin_header_uvec4_count> header{
        glm::uvec4{m_grid.counts.x, m_grid.counts.y, m_grid.counts.z, static_cast<uint32_t>(m_capacity)},
        glm::uvec4{m_category_begin[0], m_category_begin[1], m_category_begin[2], m_category_begin[3]},
        glm::uvec4{
            m_category_begin[4],
            m_open_flags,
            static_cast<uint32_t>(c_light_cluster_header_uint_count * cluster_count),
            static_cast<uint32_t>(get_fixed_list_stride())
        }
    };
    std::size_t offset = 0;
    std::memcpy(out.data() + offset, header.data(), sizeof(header));
    offset += sizeof(header);
    std::memcpy(out.data() + offset, m_slices.data(), m_slices.size() * sizeof(glm::vec4));
    offset += m_slices.size() * sizeof(glm::vec4);
    std::memcpy(out.data() + offset, m_corners.data(), m_corners.size() * sizeof(glm::vec4));
    offset += m_corners.size() * sizeof(glm::vec4);
    if (m_lights.empty()) {
        std::memset(out.data() + offset, 0, 2 * sizeof(glm::vec4));
    } else {
        std::memcpy(out.data() + offset, m_lights.data(), m_lights.size() * sizeof(glm::vec4));
        offset += m_lights.size() * sizeof(glm::vec4);
        std::memcpy(out.data() + offset, m_cones.data(), m_cones.size() * sizeof(glm::vec4));
    }
}

auto Light_cluster_binner::get_fixed_list_stride() const -> std::size_t
{
    return (m_capacity + 1) / 2;
}

auto Light_cluster_binner::get_fixed_byte_count() const -> std::size_t
{
    return m_grid.get_cluster_count() * (c_light_cluster_header_uint_count + get_fixed_list_stride()) * sizeof(uint32_t);
}

auto Light_cluster_binner::get_byte_count() const -> std::size_t
{
    const std::size_t cluster_count = m_grid.get_cluster_count();
    std::size_t uint_count = c_light_cluster_header_uint_count * cluster_count;
    for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
        const std::size_t count = m_cluster_offsets[cluster + 1] - m_cluster_offsets[cluster];
        uint_count += (count + 1) / 2;
    }
    return uint_count * sizeof(uint32_t);
}

void Light_cluster_binner::write(const std::span<std::byte> out) const
{
    ERHE_PROFILE_FUNCTION();

    const std::size_t cluster_count = m_grid.get_cluster_count();
    std::size_t list_uint = c_light_cluster_header_uint_count * cluster_count;
    for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
        const uint32_t* const category = &m_category_counts[cluster * 4];
        const std::size_t header = c_light_cluster_header_uint_count * cluster;
        write_uint(out, header + 0, static_cast<uint32_t>(list_uint));
        write_uint(out, header + 1, pack_halves(category[0], category[1]));
        write_uint(out, header + 2, pack_halves(category[2], category[3]));
        const std::size_t begin = m_cluster_offsets[cluster];
        const std::size_t end   = m_cluster_offsets[cluster + 1];
        for (std::size_t i = begin; i < end; i += 2) {
            const std::size_t high = ((i + 1) < end) ? m_sorted_slots[i + 1] : 0;
            write_uint(out, list_uint, pack_halves(m_sorted_slots[i], high));
            ++list_uint;
        }
    }
    ERHE_VERIFY((list_uint * sizeof(uint32_t)) == get_byte_count());
}

auto Light_cluster_binner::get_cluster_slots(const std::size_t cluster) const -> std::span<const uint32_t>
{
    ERHE_VERIFY((cluster + 1) < m_cluster_offsets.size());
    const std::size_t begin = m_cluster_offsets[cluster];
    const std::size_t end   = m_cluster_offsets[cluster + 1];
    return std::span<const uint32_t>{m_sorted_slots.data() + begin, end - begin};
}

auto Light_cluster_binner::get_cluster_category_counts(const std::size_t cluster) const -> std::span<const uint32_t>
{
    ERHE_VERIFY(((cluster + 1) * 4) <= m_category_counts.size());
    return std::span<const uint32_t>{m_category_counts.data() + (cluster * 4), 4};
}

} // namespace erhe::scene_renderer
