#include "erhe_scene_renderer/draw_list.hpp"

#include <glm/glm.hpp>

#include <bit>
#include <cstring>

namespace erhe::scene_renderer {

auto Cull_view_tests::is_same(const Cull_view_tests& other) const -> bool
{
    // Bitwise per float, never the objects' bytes (padding).
    const auto same_bits = [](const float a, const float b) -> bool {
        return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
    };
    if ((purpose != other.purpose) || (kind != other.kind)) {
        return false;
    }
    if (kind == Cull_view_kind::cube_face) {
        return
            (cube_face == other.cube_face) &&
            same_bits(sphere.center.x, other.sphere.center.x) &&
            same_bits(sphere.center.y, other.sphere.center.y) &&
            same_bits(sphere.center.z, other.sphere.center.z) &&
            same_bits(sphere.radius,   other.sphere.radius);
    }
    return
        (plane_count == other.plane_count) &&
        (std::memcmp(planes.data(), other.planes.data(), plane_count * sizeof(glm::vec4)) == 0) &&
        same_bits(pvs_query_min.x, other.pvs_query_min.x) &&
        same_bits(pvs_query_min.y, other.pvs_query_min.y) &&
        same_bits(pvs_query_min.z, other.pvs_query_min.z) &&
        same_bits(pvs_query_max.x, other.pvs_query_max.x) &&
        same_bits(pvs_query_max.y, other.pvs_query_max.y) &&
        same_bits(pvs_query_max.z, other.pvs_query_max.z);
}

auto Cull_view_tests::has_pvs_query() const -> bool
{
    return
        (kind == Cull_view_kind::planes) &&
        (pvs_query_max.x >= pvs_query_min.x) &&
        (pvs_query_max.y >= pvs_query_min.y) &&
        (pvs_query_max.z >= pvs_query_min.z);
}

auto is_blending_class_selected(const Draw_blending_selection selection, const erhe::primitive::Blending_class blending_class) -> bool
{
    switch (selection) {
        case Draw_blending_selection::opaque_only:       return blending_class == erhe::primitive::Blending_class::opaque;
        case Draw_blending_selection::alpha_tested_only: return blending_class == erhe::primitive::Blending_class::alpha_tested;
        case Draw_blending_selection::translucent_only:  return blending_class == erhe::primitive::Blending_class::translucent;
        case Draw_blending_selection::all:               return true;
        default:                                         return false;
    }
}

void Draw_list_bounds::resize(const std::size_t count)
{
    // Grown entries have no bounds until set() writes them: a negative
    // extent, so an entry whose bounds were never written passes every view.
    center_x.resize(count,  0.0f);
    center_y.resize(count,  0.0f);
    center_z.resize(count,  0.0f);
    extent_x.resize(count, -1.0f);
    extent_y.resize(count, -1.0f);
    extent_z.resize(count, -1.0f);
}

void Draw_list_bounds::set(const std::size_t index, const erhe::math::Aabb& aabb)
{
    if (!aabb.is_valid_3d()) {
        center_x[index] =  0.0f;
        center_y[index] =  0.0f;
        center_z[index] =  0.0f;
        extent_x[index] = -1.0f;
        extent_y[index] = -1.0f;
        extent_z[index] = -1.0f;
        return;
    }
    // The same expressions as erhe::math::aabb_in_convex_volume(), so the
    // classification's plane distances are bit-identical to that test's.
    const glm::vec3 center = aabb.center();
    const glm::vec3 extent = 0.5f * aabb.diagonal();
    center_x[index] = center.x;
    center_y[index] = center.y;
    center_z[index] = center.z;
    extent_x[index] = extent.x;
    extent_y[index] = extent.y;
    extent_z[index] = extent.z;
}

void Draw_list_bounds::move(const std::size_t from, const std::size_t to)
{
    center_x[to] = center_x[from];
    center_y[to] = center_y[from];
    center_z[to] = center_z[from];
    extent_x[to] = extent_x[from];
    extent_y[to] = extent_y[from];
    extent_z[to] = extent_z[from];
}

void Draw_list_bounds::assign(const Draw_list_bounds& source)
{
    center_x.assign(source.center_x.begin(), source.center_x.end());
    center_y.assign(source.center_y.begin(), source.center_y.end());
    center_z.assign(source.center_z.begin(), source.center_z.end());
    extent_x.assign(source.extent_x.begin(), source.extent_x.end());
    extent_y.assign(source.extent_y.begin(), source.extent_y.end());
    extent_z.assign(source.extent_z.begin(), source.extent_z.end());
}

} // namespace erhe::scene_renderer
