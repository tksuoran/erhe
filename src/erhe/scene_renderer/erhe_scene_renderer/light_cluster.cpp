#include "erhe_scene_renderer/light_cluster.hpp"
#include "erhe_scene_renderer/camera_buffer.hpp"
#include "erhe_scene_renderer/light_buffer.hpp"
#include "erhe_scene_renderer/shader_key.hpp"

#include "erhe_scene/camera_snapshot.hpp"
#include "erhe_scene/light_snapshot.hpp"
#include "erhe_profile/profile.hpp"
#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace erhe::scene_renderer {

namespace {

// Relative widening of every light's range sphere and every cluster's
// bounds: a fragment whose fp32 cluster lookup rounds across a cluster
// boundary still finds every light that reaches it (C4).
constexpr double c_bound_widening = 1.0 / 1024.0;

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
    [[nodiscard]] auto get_category(const std::size_t slot) const -> std::size_t
    {
        for (std::size_t category = 0; category < 4; ++category) {
            if (slot < begin[category + 1]) {
                return category;
            }
        }
        ERHE_FATAL("slot %zu is not a binned light", slot);
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
    const Light_projections&   light_projections,
    const Light_cluster_view&  view,
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
    m_node_from_world = glm::inverse(glm::dmat4{world_from_node});
    // The bins compare node-space depths and positions with light ranges in
    // world units, and the fragment lookup takes view depth as a dot product
    // with the node z axis: both assume the camera's world_from_node carries
    // no scale (a rotation and a translation). A scaled camera node scales
    // every cluster bound against the ranges, so its lists are not
    // conservative; the grid does not support one.
    m_view_origin     = glm::dvec3{view_origin};

    m_grid.clip_from_cluster_relative = glm::mat4{get_clip_from_view_relative(clip_from_node, world_from_node, view_origin)};
    m_grid.view_origin = glm::vec4{view_origin, 0.0f};
    // The node-space z of a point p_c relative to the camera position is the
    // z row of node_from_world's linear part dotted with p_c (the camera
    // position is the node origin); view depth is its negation.
    m_grid.view_axis = glm::vec4{
        static_cast<float>(m_node_from_world[0][2]),
        static_cast<float>(m_node_from_world[1][2]),
        static_cast<float>(m_node_from_world[2][2]),
        0.0f
    };

    const int width  = std::max(view.viewport.width,  1);
    const int height = std::max(view.viewport.height, 1);
    const uint32_t tiles_x = static_cast<uint32_t>((width  + config.tile_size - 1) / config.tile_size);
    const uint32_t tiles_y = static_cast<uint32_t>((height + config.tile_size - 1) / config.tile_size);

    // Slices: from the near plane to the farthest view depth a binned light
    // reaches.
    m_near = std::max(static_cast<double>(camera.projection().get_z_near()), 1.0e-3);
    double far = m_near;
    const Category_bounds bounds{light_projections.light_partition};
    for (std::size_t slot = bounds.begin[0]; slot < bounds.begin[4]; ++slot) {
        const erhe::scene::Light_projection_transforms& transforms = light_projections.light_projection_transforms[slot];
        const double range = static_cast<double>(transforms.light->get_range());
        if (range <= 0.0) {
            continue;
        }
        const glm::dvec3 centre = glm::dvec3{m_node_from_world * glm::dvec4{get_light_position(transforms), 1.0}};
        far = std::max(far, -centre.z + (range * (1.0 + c_bound_widening)));
    }
    const uint32_t slice_count = (far > (m_near * (1.0 + c_bound_widening)))
        ? static_cast<uint32_t>(config.slice_count)
        : 1u;
    const double log_ratio = std::log2(far / m_near);
    const double scale     = (slice_count > 1) ? (static_cast<double>(slice_count) / log_ratio) : 0.0;
    const double bias      = -std::log2(m_near) * scale;
    m_grid.slices = glm::vec4{static_cast<float>(scale), static_cast<float>(bias), static_cast<float>(m_near), 0.0f};
    m_grid.counts = glm::uvec4{tiles_x, tiles_y, slice_count, 0u};

    // Slice k spans [m_slice_depths[k], m_slice_depths[k + 1]]; slice 0
    // starts at the camera (it takes the fragments nearer than the near
    // plane), the last one ends at far.
    m_slice_depths.resize(slice_count + 1);
    m_slice_depths[0] = 0.0;
    for (uint32_t k = 1; k < slice_count; ++k) {
        m_slice_depths[k] = m_near * std::exp2((static_cast<double>(k) / static_cast<double>(slice_count)) * log_ratio);
    }
    m_slice_depths[slice_count] = std::max(far, m_near);

    // Tile corner rays: corner (ix, iy) is at normalized device position
    // (-1 + 2 ix / tiles_x, -1 + 2 iy / tiles_y), the tile lookup's
    // boundaries.
    const glm::dmat4 C{clip_from_node};
    m_corner_origins   .resize((tiles_x + 1) * (tiles_y + 1));
    m_corner_directions.resize((tiles_x + 1) * (tiles_y + 1));
    for (uint32_t iy = 0; iy <= tiles_y; ++iy) {
        const double y = -1.0 + ((2.0 * static_cast<double>(iy)) / static_cast<double>(tiles_y));
        for (uint32_t ix = 0; ix <= tiles_x; ++ix) {
            const double     x       = -1.0 + ((2.0 * static_cast<double>(ix)) / static_cast<double>(tiles_x));
            const glm::dvec3 at_0    = unproject_at_z(C, x, y,  0.0);
            const glm::dvec3 at_1    = unproject_at_z(C, x, y, -1.0);
            const std::size_t corner = (iy * (tiles_x + 1)) + ix;
            m_corner_origins   [corner] = at_0;
            m_corner_directions[corner] = at_1 - at_0;
        }
    }
    // Which view-space side the first tile column / row faces, at depth 1.
    m_x_min_at_left   = unproject_at_z(C, -1.0, 0.0, -1.0).x < unproject_at_z(C, 1.0, 0.0, -1.0).x;
    m_y_min_at_bottom = unproject_at_z(C, 0.0, -1.0, -1.0).y < unproject_at_z(C, 0.0, 1.0, -1.0).y;
}

void Light_cluster_binner::bin(
    const Light_projections&    light_projections,
    const Light_cluster_view&   view,
    const Light_cluster_config& config
)
{
    ERHE_PROFILE_FUNCTION();

    build_grid(light_projections, view, config);

    const uint32_t    tiles_x       = m_grid.counts.x;
    const uint32_t    tiles_y       = m_grid.counts.y;
    const uint32_t    slice_count   = m_grid.counts.z;
    const std::size_t cluster_count = m_grid.get_cluster_count();
    const Category_bounds bounds{light_projections.light_partition};
    ERHE_VERIFY(light_projections.light_projection_transforms.size() >= bounds.begin[4]);
    constexpr double infinity = std::numeric_limits<double>::infinity();

    m_hits.clear();
    for (std::size_t slot = bounds.begin[0]; slot < bounds.begin[4]; ++slot) {
        const erhe::scene::Light_projection_transforms& transforms = light_projections.light_projection_transforms[slot];
        const double range = static_cast<double>(transforms.light->get_range());
        if (range <= 0.0) {
            // No range: inverse square with no cutoff reaches every fragment.
            for (std::size_t cluster = 0; cluster < cluster_count; ++cluster) {
                m_hits.push_back(Hit{.cluster = static_cast<uint32_t>(cluster), .slot = static_cast<uint32_t>(slot)});
            }
            continue;
        }
        const glm::dvec3 centre = glm::dvec3{m_node_from_world * glm::dvec4{get_light_position(transforms), 1.0}};
        const double     radius = range * (1.0 + c_bound_widening);
        const double     depth  = -centre.z;
        for (uint32_t k = 0; k < slice_count; ++k) {
            const double d0 = m_slice_depths[k];
            const double d1 = m_slice_depths[k + 1];
            // Slice 0 is open toward the camera and behind it (it takes every
            // fragment nearer than the near plane), so a light behind the
            // camera can reach it.
            if ((((k > 0) && ((depth + radius) < d0))) || ((depth - radius) > d1)) {
                continue;
            }
            const double widening = c_bound_widening * std::max(d1, 1.0e-3);
            for (uint32_t iy = 0; iy < tiles_y; ++iy) {
                for (uint32_t ix = 0; ix < tiles_x; ++ix) {
                    glm::dvec3 min_corner{ infinity};
                    glm::dvec3 max_corner{-infinity};
                    for (uint32_t cy = iy; cy <= (iy + 1); ++cy) {
                        for (uint32_t cx = ix; cx <= (ix + 1); ++cx) {
                            const std::size_t corner = (cy * (tiles_x + 1)) + cx;
                            const glm::dvec3 p0 = m_corner_origins[corner] + (d0 * m_corner_directions[corner]);
                            const glm::dvec3 p1 = m_corner_origins[corner] + (d1 * m_corner_directions[corner]);
                            min_corner = glm::min(min_corner, glm::min(p0, p1));
                            max_corner = glm::max(max_corner, glm::max(p0, p1));
                        }
                    }
                    // Open boundary clusters (C2): the outer tiles reach past
                    // the frustum's sides. Slice 0 reaches toward and behind
                    // the camera and is open sideways in every tile: a
                    // fragment behind the camera (clip w <= 0) has no
                    // meaningful tile, and the lookup puts it in some slice 0
                    // tile, which then has to list every light that reaches
                    // the slice's depth range. Slice 0 is the thinnest slice,
                    // so this costs little.
                    if (ix == 0)             { if (m_x_min_at_left)   { min_corner.x = -infinity; } else { max_corner.x = infinity; } }
                    if (ix == (tiles_x - 1)) { if (m_x_min_at_left)   { max_corner.x =  infinity; } else { min_corner.x = -infinity; } }
                    if (iy == 0)             { if (m_y_min_at_bottom) { min_corner.y = -infinity; } else { max_corner.y = infinity; } }
                    if (iy == (tiles_y - 1)) { if (m_y_min_at_bottom) { max_corner.y =  infinity; } else { min_corner.y = -infinity; } }
                    if (k == 0) {
                        min_corner.x = -infinity;
                        min_corner.y = -infinity;
                        max_corner.x =  infinity;
                        max_corner.y =  infinity;
                        max_corner.z =  infinity;
                    }
                    min_corner -= glm::dvec3{widening};
                    max_corner += glm::dvec3{widening};
                    const glm::dvec3 nearest  = glm::clamp(centre, min_corner, max_corner);
                    const glm::dvec3 offset   = centre - nearest;
                    if (glm::dot(offset, offset) <= (radius * radius)) {
                        const std::size_t cluster = (((static_cast<std::size_t>(k) * tiles_y) + iy) * tiles_x) + ix;
                        m_hits.push_back(Hit{.cluster = static_cast<uint32_t>(cluster), .slot = static_cast<uint32_t>(slot)});
                    }
                }
            }
        }
    }

    // Counting sort by cluster. The hits are in ascending slot order (the
    // walk above), and the scatter keeps that order per cluster.
    const std::size_t capacity = static_cast<std::size_t>(std::clamp(config.capacity, 1, 0xffff));
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
        ++m_category_counts[(hit.cluster * 4) + bounds.get_category(hit.slot)];
    }
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

} // namespace erhe::scene_renderer
