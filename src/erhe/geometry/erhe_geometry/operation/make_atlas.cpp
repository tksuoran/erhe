#include "erhe_geometry/operation/make_atlas.hpp"
#include "erhe_geometry/operation/geometry_operation.hpp"
#include "erhe_geometry/geometry.hpp"
#include "erhe_geometry/geometry_log.hpp"

#include <spdlog/spdlog.h>

#include <geogram/mesh/mesh.h>
#include <geogram/parameterization/mesh_atlas_maker.h>
#include <geogram/parameterization/mesh_param_packer.h>
#include <geogram/basic/attributes.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <vector>

namespace erhe::geometry::operation {

namespace {

[[nodiscard]] auto to_geo(const Atlas_parameterizer parameterizer) -> GEO::ChartParameterizer
{
    switch (parameterizer) {
        case Atlas_parameterizer::projection:    return GEO::PARAM_PROJECTION;
        case Atlas_parameterizer::lscm:          return GEO::PARAM_LSCM;
        case Atlas_parameterizer::spectral_lscm: return GEO::PARAM_SPECTRAL_LSCM;
        case Atlas_parameterizer::abf:           return GEO::PARAM_ABF;
        case Atlas_parameterizer::per_facet:     return GEO::PARAM_ABF; // not reached - per_facet skips Geogram
        default:                                 return GEO::PARAM_ABF;
    }
}

[[nodiscard]] auto to_geo(const Atlas_packer packer) -> GEO::ChartPacker
{
    switch (packer) {
        case Atlas_packer::none:   return GEO::PACK_NONE;
        case Atlas_packer::tetris: return GEO::PACK_TETRIS;
        case Atlas_packer::xatlas: return GEO::PACK_XATLAS;
        default:                   return GEO::PACK_XATLAS;
    }
}

void delete_attribute_if_present(GEO::AttributesManager& manager, const char* const name)
{
    if (manager.is_defined(name)) {
        manager.delete_attribute_store(name);
    }
}

// Surface area of the (double-precision) mesh, fan triangulation per facet.
[[nodiscard]] auto mesh_surface_area(const GEO::Mesh& mesh) -> double
{
    double area = 0.0;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const GEO::vec3 p0 = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, 0)));
        for (GEO::index_t k = 2; k < corner_count; ++k) {
            const GEO::vec3 p1 = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, k - 1)));
            const GEO::vec3 p2 = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            area += 0.5 * GEO::length(GEO::cross(p1 - p0, p2 - p0));
        }
    }
    return area;
}

// Defensive repair for outlier corner UVs (observed with Geogram on coarse
// curved meshes near points where several charts meet - see
// doc/geogram_atlas_packing_feature_request.md): a facet whose corners are
// mutually consistent except for one wildly outlying value rasterizes as a
// long sliver across unrelated charts and bakes garbage. For each corner
// whose UV is far (relative to the facet's own UV extent) from the facet's
// other corners, snap it to the closest UV that the same vertex carries in
// an edge-adjacent facet; if none is closer, collapse it onto the other
// corners' centroid (a locally degenerate quad beats a cross-atlas sliver).
void repair_outlier_corner_uvs(GEO::Mesh& mesh)
{
    GEO::Attribute<double> tex_coord;
    tex_coord.bind_if_is_defined(mesh.facet_corners.attributes(), "tex_coord");
    if (!tex_coord.is_bound()) {
        return;
    }

    // corners-by-vertex adjacency
    std::vector<std::vector<GEO::index_t>> corners_of_vertex(mesh.vertices.nb());
    for (GEO::index_t facet : mesh.facets) {
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            corners_of_vertex[mesh.facet_corners.vertex(corner)].push_back(corner);
        }
    }
    const auto uv_of = [&tex_coord](const GEO::index_t corner) -> GEO::vec2 {
        return GEO::vec2{tex_coord[2 * corner + 0], tex_coord[2 * corner + 1]};
    };

    std::size_t repaired = 0;
    for (GEO::index_t facet : mesh.facets) {
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            // Extent and centroid of the facet's OTHER corners.
            GEO::vec2 centroid{0.0, 0.0};
            double    extent = 0.0;
            std::vector<GEO::vec2> others;
            for (GEO::index_t other : mesh.facets.corners(facet)) {
                if (other == corner) {
                    continue;
                }
                others.push_back(uv_of(other));
                centroid += others.back();
            }
            centroid = centroid / static_cast<double>(others.size());
            for (std::size_t i = 0; i < others.size(); ++i) {
                for (std::size_t j = i + 1; j < others.size(); ++j) {
                    extent = std::max(extent, GEO::length(others[i] - others[j]));
                }
            }
            const double distance = GEO::length(uv_of(corner) - centroid);
            if (distance <= std::max(3.0 * extent, 1.0e-12)) {
                continue; // consistent with the rest of the facet
            }
            // Outlier: choose the closest-to-centroid UV among what this
            // vertex carries elsewhere, falling back to the centroid itself.
            GEO::vec2 best      = centroid;
            double    best_dist = extent; // must beat the facet's own extent
            for (const GEO::index_t sibling : corners_of_vertex[mesh.facet_corners.vertex(corner)]) {
                if (sibling == corner) {
                    continue;
                }
                const GEO::vec2 candidate = uv_of(sibling);
                const double    d         = GEO::length(candidate - centroid);
                if (d < best_dist) {
                    best_dist = d;
                    best      = candidate;
                }
            }
            tex_coord[2 * corner + 0] = best.x;
            tex_coord[2 * corner + 1] = best.y;
            ++repaired;
        }
    }
    log_operation->warn("make_atlas: repaired {} outlier corner UVs", repaired);
}

// Per-facet unwrap (doc/lightmap_seam_driven_unwrap_plan.md phase 1): every
// facet becomes its own chart, flattened isometrically in its own plane
// (local orthonormal facet basis), so parameterization distortion is zero
// for planar facets, triangles never overlap by construction, and nothing
// shares texels. UVs are written in mesh units (UV area == 3D area), which
// is exactly the consistent scale pack_charts_with_texel_gutter() needs, so
// Geogram's normalize step is not required. Writes the same "tex_coord"
// facet-corner and "chart" facet attributes mesh_make_atlas() would.
void build_per_facet_charts(GEO::Mesh& mesh)
{
    GEO::Attribute<double> tex_coord;
    tex_coord.bind_if_is_defined(mesh.facet_corners.attributes(), "tex_coord");
    if (!tex_coord.is_bound()) {
        tex_coord.create_vector_attribute(mesh.facet_corners.attributes(), "tex_coord", 2);
    }
    GEO::Attribute<GEO::index_t> chart(mesh.facets.attributes(), "chart");

    for (GEO::index_t facet : mesh.facets) {
        chart[facet] = facet;
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            for (GEO::index_t corner : mesh.facets.corners(facet)) {
                tex_coord[2 * corner + 0] = 0.0;
                tex_coord[2 * corner + 1] = 0.0;
            }
            continue;
        }
        // Newell normal (robust for slightly non-planar polygons).
        GEO::vec3 normal{0.0, 0.0, 0.0};
        const GEO::vec3 p0 = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, 0)));
        for (GEO::index_t k = 0; k < corner_count; ++k) {
            const GEO::vec3 a = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            const GEO::vec3 b = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, (k + 1) % corner_count)));
            normal.x += (a.y - b.y) * (a.z + b.z);
            normal.y += (a.z - b.z) * (a.x + b.x);
            normal.z += (a.x - b.x) * (a.y + b.y);
        }
        const double normal_length = GEO::length(normal);
        if (normal_length > 0.0) {
            normal = normal / normal_length;
        } else {
            normal = GEO::vec3{0.0, 0.0, 1.0};
        }
        // First basis vector: the first non-degenerate edge, made orthogonal
        // to the normal.
        GEO::vec3 tangent{1.0, 0.0, 0.0};
        for (GEO::index_t k = 1; k < corner_count; ++k) {
            const GEO::vec3 p = mesh.vertices.point(mesh.facet_corners.vertex(mesh.facets.corner(facet, k)));
            GEO::vec3 edge = p - p0;
            edge = edge - GEO::dot(edge, normal) * normal;
            const double edge_length = GEO::length(edge);
            if (edge_length > 1.0e-12) {
                tangent = edge / edge_length;
                break;
            }
        }
        // cross(tangent, normal), not cross(normal, tangent): the G-buffer
        // raster's cull convention (lightmap_baker.cpp cull_mode_back_cw +
        // winding_flip_if(top_left)) is tuned to the chart orientation
        // Geogram's atlas maker emits, which is MIRRORED relative to the
        // 3D facet winding; match it or every per-facet triangle is culled
        // (observed: zero G-buffer coverage with the un-mirrored basis).
        const GEO::vec3 bitangent = GEO::cross(tangent, normal);
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            const GEO::vec3 p = mesh.vertices.point(mesh.facet_corners.vertex(corner)) - p0;
            tex_coord[2 * corner + 0] = GEO::dot(p, tangent);
            tex_coord[2 * corner + 1] = GEO::dot(p, bitangent);
        }
    }
}

// Repack the charts of a parameterized mesh so that no two charts are closer
// than gutter_texels at a rasterization resolution of target_texels (atlas
// side). Reads Geogram's "tex_coord" facet-corner attribute and "chart" facet
// attribute (both left in place by mesh_make_atlas with PACK_NONE after
// pack_atlas_only_normalize_charts), rewrites "tex_coord" into [0,1]^2.
//
// Shelf packing: charts sorted by height, placed left-to-right on shelves.
// Chart sizes are preserved (area proportionality from the normalize step);
// the gutter is a fixed fraction of the final span, iterated to convergence
// since span depends on placement.
void pack_charts_with_texel_gutter(
    GEO::Mesh&                mesh,
    const double              target_texels,
    const double              gutter_texels,
    const double              min_side_texels,
    const std::vector<float>* chart_order_keys)
{
    GEO::Attribute<double> tex_coord;
    tex_coord.bind_if_is_defined(mesh.facet_corners.attributes(), "tex_coord");
    GEO::Attribute<GEO::index_t> chart;
    chart.bind_if_is_defined(mesh.facets.attributes(), "chart");
    if (!tex_coord.is_bound() || !chart.is_bound() || (target_texels <= 0.0)) {
        return;
    }

    GEO::index_t chart_count = 0;
    for (GEO::index_t facet : mesh.facets) {
        chart_count = std::max(chart_count, chart[facet] + 1u);
    }
    if (chart_count == 0) {
        return;
    }

    struct Chart_rect
    {
        double min_u{ std::numeric_limits<double>::max()};
        double min_v{ std::numeric_limits<double>::max()};
        double max_u{-std::numeric_limits<double>::max()};
        double max_v{-std::numeric_limits<double>::max()};
        double place_u{0.0}; // placement of min corner, pre-normalization
        double place_v{0.0};
        // Minimum-resolution upscale (charts smaller than min_side_texels
        // at the target rasterization density contain no texel center and
        // bake nothing; scaling them up gives every chart at least a few
        // valid texels at the cost of slightly more atlas area). Applied
        // to the chart's own UV extent at placement and final rewrite.
        // Anisotropic: each axis is scaled independently, so a sliver
        // chart reaches the minimum in its thin axis without inflating
        // its long axis (and the atlas) with it.
        double scale_u{1.0};
        double scale_v{1.0};
        bool   used{false};
        [[nodiscard]] auto width () const -> double { return (max_u - min_u) * scale_u; }
        [[nodiscard]] auto height() const -> double { return (max_v - min_v) * scale_v; }
    };
    std::vector<Chart_rect> rects(chart_count);
    double total_uv_area = 0.0;
    for (GEO::index_t facet : mesh.facets) {
        Chart_rect& rect = rects[chart[facet]];
        const GEO::index_t corner_count = mesh.facets.nb_corners(facet);
        const GEO::index_t corner_0     = mesh.facets.corner(facet, 0);
        const double u0 = tex_coord[2 * corner_0 + 0];
        const double v0 = tex_coord[2 * corner_0 + 1];
        for (GEO::index_t k = 0; k < corner_count; ++k) {
            const GEO::index_t corner = mesh.facets.corner(facet, k);
            const double u = tex_coord[2 * corner + 0];
            const double v = tex_coord[2 * corner + 1];
            rect.min_u = std::min(rect.min_u, u);
            rect.min_v = std::min(rect.min_v, v);
            rect.max_u = std::max(rect.max_u, u);
            rect.max_v = std::max(rect.max_v, v);
            rect.used  = true;
            if (k >= 2) {
                const GEO::index_t corner_p = mesh.facets.corner(facet, k - 1);
                const double up = tex_coord[2 * corner_p + 0];
                const double vp = tex_coord[2 * corner_p + 1];
                total_uv_area += 0.5 * std::abs((up - u0) * (v - v0) - (u - u0) * (vp - v0));
            }
        }
    }

    // One texel's size in chart UV units is a CONSTANT, not a fraction of
    // the packed span: the atlas consumer (Lightmap_baker::update_layout)
    // sizes the mesh's atlas region as sqrt(world_area / uv_coverage) *
    // density, and uv_coverage is the summed facet UV area of this packing
    // - the lower the packing efficiency, the larger the region, so texel
    // size in the charts' own units never changes. target_texels estimates
    // sqrt(mesh_area) * density, and the charts' consistent scale gives
    // total_uv_area = k^2 * mesh_area, hence texel_uv = k * sqrt(mesh_area)
    // / target_texels = sqrt(total_uv_area) / target_texels. Deriving the
    // gutter and minimum chart side from the span instead (as this packer
    // originally did) is a positive feedback - requirements that grow with
    // the span being solved for - which for many-tiny-chart meshes
    // (per-facet unwraps of dense meshes) has NO fixed point under the
    // scale cap: the span diverged until the cap broke the minimum-size
    // clamp and every chart landed sub-texel (observed as uv_coverage
    // collapsing to the 5% floor for capsule / sphere / torus).
    const double texel_uv = (total_uv_area > 0.0) ? (std::sqrt(total_uv_area) / target_texels) : 0.0;
    if ((min_side_texels > 0.0) && (texel_uv > 0.0)) {
        // Per-axis (anisotropic) minimum-size clamp. The 16x cap keeps
        // truly degenerate slivers from exploding the atlas; with a
        // constant min_side_uv it can only be reached by charts whose raw
        // extent is under 1/16 texel in that axis.
        //
        // This clamp alone cannot deliver the minimum when MOST charts are
        // below it (per-facet unwraps of dense meshes): scaling every chart
        // up grows the summed UV area, and the consumer's coverage feedback
        // shrinks the region by the same factor - the texels per chart are
        // invariant. The consumer closes that gap by growing the region
        // (Lightmap_baker::update_layout min-face-texels bound); the clamp
        // here still evens out the small-chart tail so that bound stays
        // moderate.
        const double min_side_uv = min_side_texels * texel_uv;
        for (GEO::index_t i = 0; i < chart_count; ++i) {
            Chart_rect& rect = rects[i];
            if (!rect.used) {
                continue;
            }
            rect.scale_u = std::clamp(min_side_uv / std::max(rect.max_u - rect.min_u, 1.0e-12), 1.0, 16.0);
            rect.scale_v = std::clamp(min_side_uv / std::max(rect.max_v - rect.min_v, 1.0e-12), 1.0, 16.0);
        }
    }
    const double gutter = gutter_texels * texel_uv;

    std::vector<GEO::index_t> order;
    double total_area    = 0.0;
    double max_dimension = 0.0;
    for (GEO::index_t i = 0; i < chart_count; ++i) {
        Chart_rect& rect = rects[i];
        // A chart whose every UV is non-finite keeps the sentinel bounds
        // (min/max drop NaN) - its extent is garbage; drop it from packing
        // instead of feeding the sort and shelves inf/NaN sizes.
        if (rect.used && (!std::isfinite(rect.width()) || !std::isfinite(rect.height()))) {
            rect.used = false;
        }
        if (!rect.used) {
            continue;
        }
        order.push_back(i);
        total_area    = total_area + rect.width() * rect.height();
        max_dimension = std::max({max_dimension, rect.width(), rect.height()});
    }
    if (order.empty()) {
        return;
    }
    // Similar-key adjacency (leak camouflage): when the caller provides
    // baked-luminance keys, pack so that neighboring charts carry similar
    // light - consecutive placement on a shelf means atlas adjacency, so
    // cross-chart filter-tap / dilation pollution picks up similar values.
    // Shelf packing wraps the 1D placement order into rows, so key
    // similarity must hold both along a shelf and across shelves: sort
    // primarily by luminance BAND (quantiles of the key distribution), so
    // each shelf is a narrow luminance stratum and vertically adjacent
    // shelves hold adjacent strata; within a band by height (shelf
    // efficiency), then by key. The placement loop below completes this:
    // shelves BREAK at band boundaries (a bimodal key jump - e.g. a
    // capsule's dark bottom fan vs lit top fan - becomes a horizontal seam
    // between shelves instead of a side-by-side pairing mid-shelf), and
    // shelves fill BOUSTROPHEDON (alternating direction), so the key
    // sequence stays spatially continuous where one shelf wraps to the
    // next.
    // Non-finite keys (NaN texels in the bake readback the keys average)
    // map to 0: NaN would break the sort comparators' strict weak ordering
    // (NaN != x is true but neither orders before the other, so equivalence
    // loses transitivity - hardened libc++ aborts, release builds scramble).
    const auto key_of = [chart_order_keys](const GEO::index_t i) -> float {
        const float key = ((chart_order_keys != nullptr) && (i < chart_order_keys->size())) ? (*chart_order_keys)[i] : 0.0f;
        return std::isfinite(key) ? key : 0.0f;
    };
    std::vector<int> band_of((chart_order_keys != nullptr) ? chart_count : 0u, 0);
    if (chart_order_keys != nullptr) {
        std::vector<GEO::index_t> by_key = order;
        std::sort(
            by_key.begin(),
            by_key.end(),
            [&key_of](const GEO::index_t lhs, const GEO::index_t rhs) {
                const float key_lhs = key_of(lhs);
                const float key_rhs = key_of(rhs);
                return (key_lhs != key_rhs) ? (key_lhs < key_rhs) : (lhs < rhs);
            }
        );
        const std::size_t band_count = std::clamp<std::size_t>(by_key.size() / 8, 1, 16);
        for (std::size_t position = 0; position < by_key.size(); ++position) {
            band_of[by_key[position]] = static_cast<int>((position * band_count) / by_key.size());
        }
    }
    std::sort(
        order.begin(),
        order.end(),
        [&rects, chart_order_keys, &band_of, &key_of](const GEO::index_t lhs, const GEO::index_t rhs) {
            if (chart_order_keys != nullptr) {
                if (band_of[lhs] != band_of[rhs]) {
                    return band_of[lhs] < band_of[rhs];
                }
                if (rects[lhs].height() != rects[rhs].height()) {
                    return rects[lhs].height() > rects[rhs].height();
                }
                const float key_lhs = key_of(lhs);
                const float key_rhs = key_of(rhs);
                if (key_lhs != key_rhs) {
                    return key_lhs < key_rhs;
                }
                return lhs < rhs;
            }
            return rects[lhs].height() > rects[rhs].height();
        }
    );

    double span = std::max(std::sqrt(total_area) * 1.15, max_dimension + 2.0 * gutter);
    for (int iteration = 0; iteration < 6; ++iteration) {
        // The gutter and the minimum-size scales are constants in chart UV
        // units (see texel_uv above); iterating only settles the shelf wrap
        // width against the used extent.
        //
        // One gutter of margin at the atlas border too: region-edge texels
        // otherwise rasterize chart data straight against the region border.
        // Shelves are accumulated before placement so a full shelf can be
        // emitted in reverse on every other shelf (boustrophedon, keys
        // only): the sequence-adjacent charts at a shelf wrap then sit at
        // the same end of consecutive shelves, i.e. vertically adjacent.
        double       shelf_y = gutter;
        double       shelf_h = 0.0;
        double       shelf_w = 0.0; // consumed width, excluding leading gutter
        double       used_w  = 0.0;
        std::size_t  shelf_rank = 0;
        std::vector<GEO::index_t> shelf_charts;
        const auto flush_shelf = [&]() {
            if (shelf_charts.empty()) {
                return;
            }
            if ((chart_order_keys != nullptr) && ((shelf_rank & 1u) != 0u)) {
                std::reverse(shelf_charts.begin(), shelf_charts.end());
            }
            double x = gutter;
            for (const GEO::index_t i : shelf_charts) {
                rects[i].place_u = x;
                rects[i].place_v = shelf_y;
                x = x + rects[i].width() + gutter;
            }
            used_w  = std::max(used_w, x);
            shelf_y = shelf_y + shelf_h + gutter;
            shelf_h = 0.0;
            shelf_w = 0.0;
            shelf_rank++;
            shelf_charts.clear();
        };
        int previous_band = (chart_order_keys != nullptr) ? band_of[order.front()] : 0;
        for (const GEO::index_t i : order) {
            const bool band_break = (chart_order_keys != nullptr) && (band_of[i] != previous_band);
            if (chart_order_keys != nullptr) {
                previous_band = band_of[i];
            }
            if (band_break || (gutter + shelf_w + rects[i].width() + gutter > span)) {
                flush_shelf();
            }
            shelf_charts.push_back(i);
            shelf_w = shelf_w + rects[i].width() + gutter;
            shelf_h = std::max(shelf_h, rects[i].height());
        }
        flush_shelf();
        const double used_h = shelf_y; // flush already advanced past the last shelf + gutter
        const double new_span = std::max(used_w, used_h);
        if (new_span <= span * 1.0001) {
            span = std::max(new_span, max_dimension); // shrink to the used extent
            break;
        }
        span = new_span;
    }

    const double inv_span = 1.0 / span;
    for (GEO::index_t facet : mesh.facets) {
        const Chart_rect& rect = rects[chart[facet]];
        for (GEO::index_t corner : mesh.facets.corners(facet)) {
            tex_coord[2 * corner + 0] = ((tex_coord[2 * corner + 0] - rect.min_u) * rect.scale_u + rect.place_u) * inv_span;
            tex_coord[2 * corner + 1] = ((tex_coord[2 * corner + 1] - rect.min_v) * rect.scale_v + rect.place_v) * inv_span;
        }
    }
}

} // anonymous namespace

// UV atlas generation via Geogram's mesh_make_atlas(). The mesh topology is
// preserved: mesh_make_atlas() fan-triangulates each polygon internally and
// commits the resulting UVs back to every original facet corner in a "tex_coord"
// facet-corners attribute (the packer only transforms UVs, never the geometry).
// We copy the source (with all its attributes) into the destination, run the
// atlas maker, then move the UVs from Geogram's "tex_coord" attribute into the
// selected erhe corner texcoord channel. Like Smooth, no source/destination
// provenance is tracked, so post_processing() leaves the freshly written UVs
// (and all other copied attributes) untouched (empty source tables -> zero weight
// -> skipped).
class Make_atlas : public Geometry_operation
{
public:
    Make_atlas(
        const Geometry&     source,
        Geometry&           destination,
        std::size_t         usage_index,
        double              hard_angles_threshold,
        Atlas_parameterizer parameterizer,
        Atlas_packer        packer,
        double                    chart_pack_texel_density,
        double                    chart_gutter_texels,
        double                    chart_min_side_texels,
        const std::vector<float>* per_facet_chart_order)
        : Geometry_operation       {source, destination}
        , m_usage_index            {usage_index}
        , m_hard_angles_threshold  {hard_angles_threshold}
        , m_parameterizer          {parameterizer}
        , m_packer                 {packer}
        , m_chart_pack_texel_density{chart_pack_texel_density}
        , m_chart_gutter_texels    {chart_gutter_texels}
        , m_chart_min_side_texels  {chart_min_side_texels}
        , m_per_facet_chart_order  {per_facet_chart_order}
    {
    }

    void build();

private:
    std::size_t               m_usage_index;
    double                    m_hard_angles_threshold;
    Atlas_parameterizer       m_parameterizer;
    Atlas_packer              m_packer;
    double                    m_chart_pack_texel_density;
    double                    m_chart_gutter_texels;
    double                    m_chart_min_side_texels;
    const std::vector<float>* m_per_facet_chart_order;
};

void Make_atlas::build()
{
    destination.get_attributes().unbind();
    destination_mesh.copy(source_mesh, true);
    generate_mesh_atlas_texture_coordinates(
        destination_mesh,
        destination.get_attributes(),
        m_usage_index,
        m_hard_angles_threshold,
        m_parameterizer,
        m_packer,
        m_chart_pack_texel_density,
        m_chart_gutter_texels,
        m_chart_min_side_texels,
        m_per_facet_chart_order
    );
    post_processing(structural_post_process_flags);
}

void generate_mesh_atlas_texture_coordinates(
    GEO::Mesh&                mesh,
    Mesh_attributes&          attributes,
    const std::size_t         usage_index,
    const double              hard_angles_threshold,
    const Atlas_parameterizer parameterizer,
    const Atlas_packer        packer,
    const double              chart_pack_texel_density,
    const double              chart_gutter_texels,
    const double              chart_min_side_texels,
    const std::vector<float>* per_facet_chart_order)
{
    // geogram_lock() is taken only around the Geogram branch below: the
    // per-facet branch reaches no Geogram *algorithm* (see geogram_lock()
    // docs) - vertices precision toggles, facets.connect(), attribute
    // binds/creates and erhe's own packing are all mesh-local (verified
    // serial at the geogram pin: mesh.cpp has no parallel_for / progress
    // tasks; attribute stores are per-mesh with spinlocked observers and
    // read-only type registries) - so concurrent per-facet unwraps of
    // DIFFERENT meshes may run in parallel on worker threads.

    // Precondition: attributes is UNBOUND (see header). mesh_make_atlas() reads
    // vertices.point() in double precision and grows charts across facet
    // adjacency, so ensure both are in place before the call.
    mesh.vertices.set_double_precision();
    mesh.facets.connect();

    // Texel-density-aware chart packing (see header): Geogram must skip its
    // own packing; the charts are packed below at the resolution this unwrap
    // will actually rasterize at. Per-facet mode always packs here (its UVs
    // start unpacked in mesh units); without a caller density it assumes a
    // 1024-texel page for gutter sizing.
    const bool   per_facet    = parameterizer == Atlas_parameterizer::per_facet;
    const bool   own_packing  = (chart_pack_texel_density > 0.0) || per_facet;
    const double target_texels = (chart_pack_texel_density > 0.0)
        ? std::max(4.0, std::ceil(std::sqrt(mesh_surface_area(mesh)) * chart_pack_texel_density))
        : (per_facet ? 1024.0 : 0.0);

    if (per_facet) {
        // No Geogram involvement (and so no serialization mutex): trivial
        // isometric per-facet charts + erhe's own packing. Nothing to
        // repair - overlaps and outlier corners are impossible here.
        build_per_facet_charts(mesh);
        pack_charts_with_texel_gutter(mesh, target_texels, chart_gutter_texels, chart_min_side_texels, per_facet_chart_order);
        log_operation->warn(
            "make_atlas: per-facet charts, {} facets, target_texels = {}",
            mesh.facets.nb(), target_texels
        );
    } else {
        // Geogram algorithms (mesh_make_atlas / chart parameterization /
        // pack_atlas_only_normalize_charts) - see geogram_lock(). Geogram's
        // progress system uses a process-global task stack, its Windows
        // thread-pool manager corrupts thread-id assignment when
        // parallel_for is entered from two threads at once, and the atlas
        // packer/parameterizer carries further process-global state; the
        // editor builds brushes / async unwraps on many worker threads, so
        // the Geogram calls must be serialized (observed pre-serialization:
        // progress.cpp end_task() assertion and intermittent heap corruption
        // inside the packer).
        const std::lock_guard<std::recursive_mutex> geogram_guard{geogram_lock()};
        GEO::mesh_make_atlas(
            mesh,
            hard_angles_threshold,
            to_geo(parameterizer),
            own_packing ? GEO::PACK_NONE : to_geo(packer),
            false // verbose
        );
        log_operation->warn(
            "make_atlas: hard_angles = {}, own_packing = {}, target_texels = {}",
            hard_angles_threshold, own_packing, target_texels
        );
        if (own_packing) {
            // Consistent chart scale (UV area proportional to 3D area);
            // charts stay unpacked and may overlap until repacked below.
            GEO::pack_atlas_only_normalize_charts(mesh);
            // Chart ids here are Geogram chart ids, not facet ids - the
            // per-facet order keys do not apply.
            pack_charts_with_texel_gutter(mesh, target_texels, chart_gutter_texels, chart_min_side_texels, nullptr);
        }
        // Repair AFTER packing: outlier detection needs all charts at a
        // single consistent scale ([0,1] atlas space); pre-normalize the
        // per-chart scales differ and mask the outliers.
        repair_outlier_corner_uvs(mesh);
    }

    mesh.vertices.set_single_precision();
    attributes.bind();

    // Move Geogram's per-corner "tex_coord" (double[2]) into the selected erhe
    // corner texcoord channel, overwriting it. The scoped Attribute is unbound at
    // the end of the block before the store is deleted below (delete asserts that
    // the store has no live observers).
    {
        GEO::Attribute<double> tex_coord;
        tex_coord.bind_if_is_defined(mesh.facet_corners.attributes(), "tex_coord");
        if (tex_coord.is_bound()) {
            Attribute_present<GEO::vec2f>& corner_texcoord = attributes.corner_texcoord(usage_index);
            for (GEO::index_t corner : mesh.facet_corners) {
                corner_texcoord.set(
                    corner,
                    GEO::vec2f{
                        static_cast<float>(tex_coord[(2 * corner) + 0]),
                        static_cast<float>(tex_coord[(2 * corner) + 1])
                    }
                );
            }
        }
    }

    // Drop the scratch attributes mesh_make_atlas() leaves behind so they are not
    // carried forward by later operations or serialization. (erhe's own "id" is a
    // facet attribute; Geogram's "id" here is a vertex attribute - different store.)
    delete_attribute_if_present(mesh.facet_corners.attributes(), "tex_coord");
    delete_attribute_if_present(mesh.facets.attributes(),        "chart");
    delete_attribute_if_present(mesh.vertices.attributes(),      "id");
}

void make_atlas(
    const Geometry&     source,
    Geometry&           destination,
    const std::size_t   usage_index,
    const double        hard_angles_threshold,
    Atlas_parameterizer parameterizer,
    Atlas_packer        packer,
    const double              chart_pack_texel_density,
    const double              chart_gutter_texels,
    const double              chart_min_side_texels,
    const std::vector<float>* per_facet_chart_order)
{
    Make_atlas operation{source, destination, usage_index, hard_angles_threshold, parameterizer, packer, chart_pack_texel_density, chart_gutter_texels, chart_min_side_texels, per_facet_chart_order};
    operation.build();
}

} // namespace erhe::geometry::operation
