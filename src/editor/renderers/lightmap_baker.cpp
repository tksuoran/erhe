#include "renderers/lightmap_baker.hpp"

#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"

#include "SkylineBinPack.h" // RectangleBinPack

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <cmath>

namespace editor {

namespace {

// Local-space surface area of a polygon mesh (fan triangulation per facet).
[[nodiscard]] auto mesh_surface_area(const GEO::Mesh& mesh) -> float
{
    double area = 0.0;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const GEO::vec3f p0 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, 0)));
        for (GEO::index_t k = 2; k < corner_count; ++k) {
            const GEO::vec3f p1 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k - 1)));
            const GEO::vec3f p2 = erhe::geometry::get_pointf(mesh.vertices, mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            area += 0.5 * GEO::length(GEO::cross(p1 - p0, p2 - p0));
        }
    }
    return static_cast<float>(area);
}

// Area scale factor of a world transform: uniform-scale approximation from
// the 3x3 determinant (|det|^(2/3)); exact for uniform scale, close enough
// for texel-density purposes otherwise.
[[nodiscard]] auto area_scale(const glm::mat4& world_from_node) -> float
{
    const float det = glm::determinant(glm::mat3{world_from_node});
    return std::pow(std::abs(det), 2.0f / 3.0f);
}

} // anonymous namespace

auto Lightmap_baker::update_layout(Scene_root& scene_root, const float texels_per_meter) -> bool
{
    m_layout = Atlas_layout{};

    std::vector<Instance_region> regions;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const float instance_area_scale = area_scale(node->world_from_node());
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const erhe::primitive::Primitive* const primitive = primitives[primitive_index].primitive.get();
            if ((primitive == nullptr) || !primitive->render_shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
            if (!geometry) {
                continue;
            }
            // Only primitives that have lightmap UVs (channel 2) participate;
            // Generate Lightmap UVs in the Lightmap window produces them.
            erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            if (!attributes.corner_texcoord_2.has(0)) {
                continue;
            }
            Instance_region region;
            region.mesh            = mesh;
            region.primitive_index = primitive_index;
            region.world_area      = mesh_surface_area(geometry->get_mesh()) * instance_area_scale;
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty()) {
        return false;
    }

    // Region content side in texels; the normalized per-mesh chart set is
    // square, so the region is too.
    const auto side_of = [texels_per_meter](const Instance_region& region) -> int {
        const float side = std::sqrt(std::max(region.world_area, 0.0f)) * texels_per_meter;
        return std::clamp(static_cast<int>(std::ceil(side)), 4, s_max_page - 2 * s_padding);
    };

    // Big regions first packs tighter with the skyline heuristic.
    std::sort(
        regions.begin(),
        regions.end(),
        [&](const Instance_region& lhs, const Instance_region& rhs) { return side_of(lhs) > side_of(rhs); }
    );

    for (int page = s_min_page; page <= s_max_page; page *= 2) {
        rbp::SkylineBinPack packer;
        packer.Init(page, page, false);
        bool failed = false;
        for (Instance_region& region : regions) {
            const int side = side_of(region);
            const rbp::Rect rect = packer.Insert(side + 2 * s_padding, side + 2 * s_padding, rbp::SkylineBinPack::LevelBottomLeft);
            if ((rect.width == 0) || (rect.height == 0)) {
                failed = true;
                break;
            }
            region.x      = rect.x + s_padding;
            region.y      = rect.y + s_padding;
            region.width  = side;
            region.height = side;
        }
        if (failed) {
            continue;
        }
        const float inv_page = 1.0f / static_cast<float>(page);
        for (Instance_region& region : regions) {
            region.uv_scale_offset = glm::vec4{
                static_cast<float>(region.width)  * inv_page,
                static_cast<float>(region.height) * inv_page,
                static_cast<float>(region.x)      * inv_page,
                static_cast<float>(region.y)      * inv_page
            };
        }
        m_layout.width   = page;
        m_layout.height  = page;
        m_layout.regions = std::move(regions);
        return true;
    }
    // Even the largest page failed; drop the layout (a later change can
    // add multi-page support - plan keeps pages <= 4096^2).
    return false;
}

} // namespace editor
