#include "level/foliage_sampler.hpp"

#include "erhe_verify/verify.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace editor {

namespace {

// A 24-bit draw scaled by a power of two into [-1, 1): exact in float on every platform.
[[nodiscard]] auto to_signed_unit(const std::uint32_t draw_24) -> float
{
    const float value  = static_cast<float>(draw_24);
    const float scaled = value * (1.0f / 8388608.0f);
    return scaled - 1.0f;
}

[[nodiscard]] auto next_draw_24(Kit_random& random) -> std::uint32_t
{
    return random.next_u32() >> 8;
}

[[nodiscard]] auto get_distance_squared(const glm::vec3& a, const glm::vec3& b) -> float
{
    const float dx  = a.x - b.x;
    const float dy  = a.y - b.y;
    const float dz  = a.z - b.z;
    const float dxx = dx * dx;
    const float dyy = dy * dy;
    const float dzz = dz * dz;
    const float sum = dxx + dyy;
    return sum + dzz;
}

[[nodiscard]] auto is_far_enough(const glm::vec3& point, const float radius, const Foliage_index_entry& other) -> bool
{
    const float limit    = std::max(radius, other.radius);
    const float limit_sq = limit * limit;
    return get_distance_squared(point, other.position) >= limit_sq;
}

[[nodiscard]] auto is_finite(const glm::vec3& v) -> bool
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

[[nodiscard]] auto get_length_squared(const glm::vec3& v) -> float
{
    const float xx  = v.x * v.x;
    const float yy  = v.y * v.y;
    const float zz  = v.z * v.z;
    const float sum = xx + yy;
    return sum + zz;
}

[[nodiscard]] auto divide(const glm::vec3& v, const float divisor) -> glm::vec3
{
    const float x = v.x / divisor;
    const float y = v.y / divisor;
    const float z = v.z / divisor;
    return glm::vec3{x, y, z};
}

[[nodiscard]] auto cross(const glm::vec3& a, const glm::vec3& b) -> glm::vec3
{
    const float yz = a.y * b.z;
    const float zy = a.z * b.y;
    const float zx = a.z * b.x;
    const float xz = a.x * b.z;
    const float xy = a.x * b.y;
    const float yx = a.y * b.x;
    const float x  = yz - zy;
    const float y  = zx - xz;
    const float z  = xy - yx;
    return glm::vec3{x, y, z};
}

} // namespace

IFoliage_ray::~IFoliage_ray() noexcept = default;

auto make_foliage_paint_random(const int seed) -> Kit_random
{
    return Kit_random{seed, c_foliage_paint_stream};
}

auto make_foliage_remove_random(const int seed) -> Kit_random
{
    return Kit_random{seed, c_foliage_remove_stream};
}

auto get_attempt_count(const float density, const float radius) -> std::size_t
{
    const float radius_sq = radius * radius;
    const float area      = std::numbers::pi_v<float> * radius_sq;
    const float product   = density * area;
    const float count     = std::round(product);
    if (!(count >= static_cast<float>(c_foliage_min_attempts))) {
        return c_foliage_min_attempts;
    }
    if (count >= static_cast<float>(c_foliage_max_attempts)) {
        return c_foliage_max_attempts;
    }
    return static_cast<std::size_t>(count);
}

auto make_brush_frame(const glm::vec3 center, const glm::vec3 normal) -> Foliage_brush_frame
{
    ERHE_VERIFY(is_finite(center));
    ERHE_VERIFY(is_finite(normal));
    const float normal_length_sq = get_length_squared(normal);
    ERHE_VERIFY(std::isfinite(normal_length_sq) && (normal_length_sq > 0.0f));
    const float     normal_length = std::sqrt(normal_length_sq);
    const glm::vec3 n             = divide(normal, normal_length);
    const float ax = std::abs(n.x);
    const float ay = std::abs(n.y);
    const float az = std::abs(n.z);
    glm::vec3 axis{1.0f, 0.0f, 0.0f};
    if ((ay < ax) && (ay <= az)) {
        axis = glm::vec3{0.0f, 1.0f, 0.0f};
    } else if ((az < ax) && (az < ay)) {
        axis = glm::vec3{0.0f, 0.0f, 1.0f};
    }
    const glm::vec3 across         = cross(n, axis);
    const float     across_length  = std::sqrt(get_length_squared(across));
    const glm::vec3 tangent0       = divide(across, across_length);
    const glm::vec3 tangent1       = cross(n, tangent0);
    return Foliage_brush_frame{
        .center   = center,
        .normal   = n,
        .tangent0 = tangent0,
        .tangent1 = tangent1
    };
}

auto make_candidate_ray(
    const Foliage_brush_frame& frame,
    const float                radius,
    const std::uint32_t        draw_x,
    const std::uint32_t        draw_y,
    Foliage_candidate_ray&     out_ray
) -> bool
{
    ERHE_VERIFY(std::isfinite(radius) && (radius > 0.0f));
    const float x  = to_signed_unit(draw_x);
    const float y  = to_signed_unit(draw_y);
    const float xx = x * x;
    const float yy = y * y;
    const float rr = xx + yy;
    if (!(rr < 1.0f)) {
        return false;
    }
    const float     rest    = 1.0f - rr;
    const float     root    = std::sqrt(rest);
    const float     h       = radius * root;
    const float     rx      = radius * x;
    const float     ry      = radius * y;
    const glm::vec3 offset0 = frame.tangent0 * rx;
    const glm::vec3 offset1 = frame.tangent1 * ry;
    const glm::vec3 lift    = frame.normal * h;
    const glm::vec3 across  = frame.center + offset0;
    const glm::vec3 plane   = across + offset1;
    out_ray.origin    = plane + lift;
    out_ray.direction = -frame.normal;
    out_ray.length    = 2.0f * h;
    return true;
}

auto is_spacing_free(
    const Foliage_index&                index,
    const glm::vec3                     point,
    const float                         radius,
    std::vector<Foliage_index::Handle>& scratch_handles
) -> bool
{
    const float reach  = std::max(radius, index.get_max_radius());
    const float margin = reach * c_foliage_spacing_query_margin;
    const float query  = reach + margin;
    index.find_within(point, query, scratch_handles);
    for (const Foliage_index::Handle handle : scratch_handles) {
        if (!is_far_enough(point, radius, index.get_entry(handle))) {
            return false;
        }
    }
    return true;
}

auto is_spacing_free(const std::span<const Foliage_index_entry> stamp_points, const glm::vec3 point, const float radius) -> bool
{
    for (const Foliage_index_entry& other : stamp_points) {
        if (!is_far_enough(point, radius, other)) {
            return false;
        }
    }
    return true;
}

Foliage_sampler_palette::Foliage_sampler_palette(const std::span<const Foliage_sampler_entry> entries)
{
    assign(entries);
}

void Foliage_sampler_palette::assign(const std::span<const Foliage_sampler_entry> entries)
{
    m_entries.assign(entries.begin(), entries.end());
    m_scatter_items.clear();
    m_total_weight = 0;
    for (const Foliage_sampler_entry& entry : entries) {
        m_scatter_items.push_back(Repeat_scatter_item{.weight = entry.weight, .radius = entry.radius});
        m_total_weight += entry.weight;
    }
}

auto Foliage_sampler_palette::get_entries() const -> std::span<const Foliage_sampler_entry>
{
    return m_entries;
}

auto Foliage_sampler_palette::get_scatter_items() const -> std::span<const Repeat_scatter_item>
{
    return m_scatter_items;
}

auto Foliage_sampler_palette::get_total_weight() const -> std::uint64_t
{
    return m_total_weight;
}

auto run_foliage_paint_stamp(
    Kit_random&                     random,
    const Foliage_brush_frame&      frame,
    const float                     radius,
    const float                     density,
    const Foliage_sampler_palette&  palette,
    const Foliage_index&            index,
    IFoliage_ray&                   ray,
    Foliage_sampler_scratch&        scratch,
    std::vector<Foliage_placement>& out_placements
) -> Foliage_stamp_counts
{
    out_placements.clear();
    scratch.stamp_points.clear();
    Foliage_stamp_counts counts;
    const std::uint64_t total_weight = palette.get_total_weight();
    if (total_weight == 0) {
        return counts;
    }
    const std::span<const Repeat_scatter_item>   items   = palette.get_scatter_items();
    const std::span<const Foliage_sampler_entry> entries = palette.get_entries();
    const std::size_t attempt_count = get_attempt_count(density, radius);
    for (std::size_t attempt = 0; attempt < attempt_count; ++attempt) {
        ++counts.attempts;
        const std::uint32_t pick_draw   = random.next_u32();
        const std::size_t   entry       = pick_scatter_item(items, total_weight, pick_draw);
        const float         entry_radius = items[entry].radius;
        bool                accepted     = false;
        Foliage_ray_hit     hit;
        for (std::size_t candidate = 0; candidate < c_foliage_candidates_per_attempt; ++candidate) {
            // A spent candidate consumes exactly its two draws.
            const std::uint32_t draw_x = next_draw_24(random);
            const std::uint32_t draw_y = next_draw_24(random);
            Foliage_candidate_ray candidate_ray;
            if (!make_candidate_ray(frame, radius, draw_x, draw_y, candidate_ray)) {
                continue;
            }
            const Foliage_ray_outcome outcome = ray.find_surface(candidate_ray, hit);
            if (outcome == Foliage_ray_outcome::no_surface) {
                ++counts.without_surface;
                continue;
            }
            if (outcome == Foliage_ray_outcome::sealed_support) {
                ++counts.on_sealed_support;
                continue;
            }
            if (!is_spacing_free(index, hit.point, entry_radius, scratch.handles) ||
                !is_spacing_free(scratch.stamp_points, hit.point, entry_radius)) {
                ++counts.refused_by_spacing;
                continue;
            }
            accepted = true;
            break;
        }
        if (!accepted) {
            continue;
        }
        const Foliage_sampler_entry& ranges     = entries[entry];
        const std::uint32_t          yaw_draw   = next_draw_24(random);
        const std::uint32_t          variant    = random.next_u32() % std::max(ranges.variants, 1u);
        const float                  yaw_unit   = static_cast<float>(yaw_draw) * (1.0f / 16777216.0f);
        const float                  yaw_range  = ranges.yaw_max_degrees - ranges.yaw_min_degrees;
        const float                  yaw_offset = yaw_range * yaw_unit;
        const float                  yaw        = ranges.yaw_min_degrees + yaw_offset;
        scratch.stamp_points.push_back(
            Foliage_index_entry{.position = hit.point, .radius = entry_radius, .key = out_placements.size()}
        );
        out_placements.push_back(
            Foliage_placement{
                .point       = hit.point,
                .normal      = hit.normal,
                .support     = hit.support,
                .entry       = entry,
                .yaw_degrees = yaw,
                .variant     = variant
            }
        );
        ++counts.placed;
    }
    return counts;
}

auto get_remove_threshold(const float remove_density) -> std::uint32_t
{
    if (!(remove_density > 0.0f)) {
        return 0;
    }
    if (remove_density >= 1.0f) {
        return c_foliage_draw_24_range;
    }
    // A product with a power of two is exact; the ceiling counts the draws strictly below it.
    const float scaled = remove_density * static_cast<float>(c_foliage_draw_24_range);
    return static_cast<std::uint32_t>(std::ceil(scaled));
}

auto is_remove_draw(const std::uint32_t draw_24, const std::uint32_t threshold) -> bool
{
    return draw_24 < threshold;
}

auto draw_remove(Kit_random& random, const std::uint32_t threshold) -> bool
{
    return is_remove_draw(next_draw_24(random), threshold);
}

auto is_before_in_remove_order(const Foliage_index_entry& lhs, const Foliage_index_entry& rhs) -> bool
{
    if (lhs.position.x != rhs.position.x) {
        return lhs.position.x < rhs.position.x;
    }
    if (lhs.position.y != rhs.position.y) {
        return lhs.position.y < rhs.position.y;
    }
    if (lhs.position.z != rhs.position.z) {
        return lhs.position.z < rhs.position.z;
    }
    return lhs.key < rhs.key;
}

void sort_in_remove_order(const std::span<Foliage_index_entry> entries)
{
    std::sort(entries.begin(), entries.end(), is_before_in_remove_order);
}

} // namespace editor
