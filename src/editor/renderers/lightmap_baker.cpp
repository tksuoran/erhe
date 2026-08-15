#include "renderers/lightmap_baker.hpp"
#include "renderers/lightmap_partitioner.hpp"
#include "renderers/lightmap_report.hpp"
#include "renderers/lightmap_tile_io.hpp"
#include "editor_log.hpp"

#include "scene/scene_root.hpp"

#include "erhe_geometry/geometry.hpp"
#include "erhe_graphics/acceleration_structure.hpp"
#include "erhe_graphics/bind_group_layout.hpp"
#include "erhe_graphics/compute_command_encoder.hpp"
#include "erhe_graphics/compute_pipeline_state.hpp"
#include "erhe_graphics/sampler.hpp"
#include "erhe_graphics/blit_command_encoder.hpp"
#include "erhe_graphics/buffer.hpp"
#include "erhe_graphics/command_buffer.hpp"
#include "erhe_graphics/device.hpp"
#include "erhe_graphics/fragment_output.hpp"
#include "erhe_graphics/fragment_outputs.hpp"
#include "erhe_graphics/image_writer.hpp"
#include "erhe_graphics/render_command_encoder.hpp"
#include "erhe_graphics/render_pass.hpp"
#include "erhe_graphics/render_pipeline.hpp"
#include "erhe_graphics/ring_buffer.hpp"
#include "erhe_graphics/ring_buffer_client.hpp"
#include "erhe_graphics/ring_buffer_range.hpp"
#include "erhe_graphics/shader_resource.hpp"
#include "erhe_graphics/shader_stages.hpp"
#include "erhe_graphics/state/vertex_input_state.hpp"
#include "erhe_graphics/texture.hpp"
#include "erhe_item/item.hpp"
#include "erhe_primitive/buffer_mesh.hpp"
#include "erhe_primitive/material.hpp"
#include "erhe_primitive/primitive.hpp"
#include "erhe_scene/light.hpp"
#include "erhe_scene/mesh.hpp"
#include "erhe_scene/node.hpp"
#include "erhe_scene/scene.hpp"
#include "erhe_math/math_util.hpp"
#include "erhe_scene_renderer/mesh_memory.hpp"
#include "erhe_verify/verify.hpp"

#include <fmt/format.h>

#include "SkylineBinPack.h" // RectangleBinPack

#include <geogram/mesh/mesh.h>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

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

// Chart-space UV metrics of one region (see Instance_region::uv_coverage /
// min_facet_uv_extent): summed facet UV area (fan triangles) in the [0,1]^2
// chart space, and the smallest facet UV AABB shorter-axis extent.
void compute_region_uv_metrics(erhe::geometry::Geometry& geometry, Lightmap_baker::Instance_region& region)
{
    erhe::geometry::Mesh_attributes& attributes = geometry.get_attributes();
    const GEO::Mesh& geo_mesh = geometry.get_mesh();
    float coverage         = 0.0f;
    float min_facet_extent = std::numeric_limits<float>::max();
    for (GEO::index_t facet : geo_mesh.facets) {
        const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
        if (corner_count < 3) {
            continue;
        }
        const std::optional<GEO::vec2f> uv0 = attributes.corner_texcoord_2.try_get(geo_mesh.facets.corner(facet, 0));
        if (!uv0.has_value()) {
            continue;
        }
        GEO::vec2f uv_min = uv0.value();
        GEO::vec2f uv_max = uv0.value();
        for (GEO::index_t k = 1; k < corner_count; ++k) {
            const std::optional<GEO::vec2f> uv = attributes.corner_texcoord_2.try_get(geo_mesh.facets.corner(facet, k));
            if (!uv.has_value()) {
                continue;
            }
            uv_min.x = std::min(uv_min.x, uv.value().x);
            uv_min.y = std::min(uv_min.y, uv.value().y);
            uv_max.x = std::max(uv_max.x, uv.value().x);
            uv_max.y = std::max(uv_max.y, uv.value().y);
            if (k >= 2) {
                const std::optional<GEO::vec2f> uv1 = attributes.corner_texcoord_2.try_get(geo_mesh.facets.corner(facet, k - 1));
                if (uv1.has_value()) {
                    const GEO::vec2f e1 = uv1.value() - uv0.value();
                    const GEO::vec2f e2 = uv.value()  - uv0.value();
                    coverage += 0.5f * std::abs(e1.x * e2.y - e1.y * e2.x);
                }
            }
        }
        const float extent = std::min(uv_max.x - uv_min.x, uv_max.y - uv_min.y);
        if (extent > 0.0f) {
            min_facet_extent = std::min(min_facet_extent, extent);
        }
    }
    // Floor at 5% (worst allowed boost ~4.5x per axis): lower coverage means
    // broken or absurdly gutter-dominated UVs, where growing the region
    // without bound would explode the page instead of fixing anything.
    region.uv_coverage         = std::clamp(coverage, 0.05f, 1.0f);
    region.min_facet_uv_extent = (min_facet_extent < std::numeric_limits<float>::max()) ? min_facet_extent : 1.0f;
}

// Region content side in texels at full density; the normalized per-mesh
// chart set is square, so the region is too. The min-face-texels bound grows
// the region until its smallest facet spans min_face_texels on its shorter
// UV axis - the half of the minimum-size guarantee the unwrap cannot provide
// (see Instance_region::min_facet_uv_extent), capped at 4x the density side
// so one degenerate sliver facet cannot explode the tile.
[[nodiscard]] auto desired_region_side(const Lightmap_baker::Instance_region& region, const float texels_per_meter, const float min_face_texels) -> float
{
    float side = std::sqrt(std::max(region.world_area, 0.0f) / region.uv_coverage) * texels_per_meter;
    if ((min_face_texels > 0.0f) && (region.min_facet_uv_extent > 0.0f)) {
        const float bound = min_face_texels / region.min_facet_uv_extent;
        side = std::max(side, std::min(bound, 4.0f * side));
    }
    return std::max(side, 4.0f);
}

// World-space AABB of a region's primitive (instance transform applied);
// degenerate/missing bounds collapse to the node origin. Grid occupancy and
// camera ranking both use this.
[[nodiscard]] auto region_world_bounds(const Lightmap_baker::Instance_region& region) -> erhe::math::Aabb
{
    erhe::math::Aabb bounds{};
    const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
    const glm::mat4 world_from_node = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
    erhe::math::Aabb local_bounds{};
    if (region.mesh) {
        const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
        if ((region.primitive_index < primitives.size()) && primitives[region.primitive_index].primitive) {
            local_bounds = primitives[region.primitive_index].primitive->get_bounding_box();
        }
    }
    if (local_bounds.is_valid()) {
        bounds = local_bounds.transformed_by(world_from_node);
    } else {
        bounds.include(glm::vec3{world_from_node[3]});
    }
    return bounds;
}

// Nominal chart coverage assumed by the pre-unwrap split estimate
// (compute_tile_split_estimate): per-facet packing efficiency is predictable
// enough that a constant suffices - the estimate only places tile
// BOUNDARIES, and the partitioned relayout re-packs every piece with its
// measured coverage. A geometric min_facet proxy
// (sqrt(min_facet_world_area * coverage / world_area)) could sharpen the
// estimate if per-tile density-flex warnings ever become common.
constexpr float c_estimated_uv_coverage = 0.7f;

// Per-draw UBO offsets must satisfy the device's uniform alignment; 256 is
// the specification maximum, valid everywhere.
constexpr std::size_t c_draw_ubo_stride = 256;

// Working-set formats, chosen per consumer precision needs (the whole set
// is 8 page-sized targets, so every byte per texel is a page^2 cost):
// - position / smooth position: world-space meters, fp32 mandatory (fp16
//   quantizes to centimeters at room scale; ray origins must be exact).
// - normal: unit vector, fp16 ample (adjust + denoise guide + gather).
// - albedo: bounce modulation + denoise guide, 8-bit unorm like the source
//   base-color textures.
// - atlas (working / dilate scratch / display): published radiance
//   AVERAGES, fp16 like typical shipped HDR lightmaps. The accumulation
//   target stays fp32: it holds a growing sum + sample count, where fp16
//   would stall accumulation after a few hundred sweeps.
constexpr erhe::dataformat::Format c_position_format = erhe::dataformat::Format::format_32_vec4_float;
constexpr erhe::dataformat::Format c_normal_format   = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_albedo_format   = erhe::dataformat::Format::format_8_vec4_unorm;
constexpr erhe::dataformat::Format c_atlas_format    = erhe::dataformat::Format::format_16_vec4_float;
constexpr erhe::dataformat::Format c_accum_format    = erhe::dataformat::Format::format_32_vec4_float;

// GLSL storage-image format qualifiers matching the constants above; the
// bind group layout declarations must agree with the actual image format.
constexpr const char* c_normal_image_format = "rgba16f";
constexpr const char* c_albedo_image_format = "rgba8";
constexpr const char* c_atlas_image_format  = "rgba16f";
constexpr const char* c_accum_image_format  = "rgba32f";

// Per-tick ray budget for the interactive loop, as a texel count: the tile
// cursor walks the atlas in horizontal bands of at most this many texels
// per frame (whole-atlas sweeps on small pages, banded on large ones).
constexpr int c_texels_per_tick = 1 << 18;

// Texel supersampling grid sides (Frostbite Flux, slide "Texel sampling
// (2)"): sample points per texel = factor^2 on a regular grid ("regular
// grid also works just fine"). Bake_options::supersample_factor selects
// 4 (16 points; cheaper hi-res origin target) or 8 (64 points, the Flux
// default; one RGBA32F page at 8x resolution per axis = 1 KB per atlas
// texel while baking).
// Hi-res origin target dimension guard; pages whose supersampled raster
// would exceed this skip the feature (logged) instead of failing texture
// creation.
constexpr int c_supersample_max_dim = 16384;

// Bytes one PAGE texel costs in targets that persist for the whole layout:
// the per-tile fp32 accumulation textures (page area in total when every
// tile is active) and the page-sized fp16 display atlas. Drives the
// budget-derived page cap in update_layout().
[[nodiscard]] auto persistent_bytes_per_texel() -> uint64_t
{
    return static_cast<uint64_t>(
        erhe::dataformat::get_format_size_bytes(c_accum_format) +   // per-tile accumulation
        erhe::dataformat::get_format_size_bytes(c_atlas_format)     // display
    );
}

// Bytes one CELL texel costs in the scratch working set (one tile resident
// at a time): the four G-buffer targets plus working atlas + dilate
// scratch. The supersample origin target is guarded separately (its own
// byte-budget check in ensure_gbuffer_targets).
[[nodiscard]] auto scratch_bytes_per_texel() -> uint64_t
{
    return static_cast<uint64_t>(
        erhe::dataformat::get_format_size_bytes(c_position_format) +    // G-buffer position
        erhe::dataformat::get_format_size_bytes(c_normal_format)   +    // G-buffer normal
        erhe::dataformat::get_format_size_bytes(c_albedo_format)   +    // G-buffer albedo
        erhe::dataformat::get_format_size_bytes(c_position_format) +    // G-buffer smooth position
        2 * erhe::dataformat::get_format_size_bytes(c_atlas_format)     // working atlas / dilate scratch
    );
}

// Fraction of the remaining device-local budget the bake working set may
// occupy. Deliberately conservative: scene textures/buffers keep streaming
// in after the layout is chosen, and driver budgets shift with what other
// processes do.
constexpr uint64_t c_budget_numerator   = 2;
constexpr uint64_t c_budget_denominator = 3;

// Minimal half->float (inf/nan pass through; fp16 denormals decode to 0 -
// nothing here cares about values below 6e-5).
[[nodiscard]] auto half_to_float(const uint16_t h) -> float
{
    const uint32_t sign     = static_cast<uint32_t>(h & 0x8000u) << 16;
    const uint32_t exponent = (h >> 10) & 0x1Fu;
    const uint32_t mantissa = h & 0x3FFu;
    uint32_t bits = 0;
    if (exponent == 0x1Fu) {
        bits = sign | 0x7F800000u | (mantissa << 13);
    } else if (exponent != 0) {
        bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
    }
    float value;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

// Full-texture RGBA readback to host floats, decoding per the texture's
// actual format (the working-set targets are a mix of fp32 / fp16 / unorm8
// now). Standalone submit + wait idle - debug/readback paths only.
[[nodiscard]] auto read_rgba_texture_to_float(
    erhe::graphics::Device&  graphics_device,
    erhe::graphics::Texture& texture,
    std::vector<float>&      out_data
) -> bool
{
    using namespace erhe::graphics;
    const int                      width       = texture.get_width();
    const int                      height      = texture.get_height();
    const erhe::dataformat::Format format      = texture.get_pixelformat();
    const std::size_t              texel_bytes = erhe::dataformat::get_format_size_bytes(format);
    if ((width <= 0) || (height <= 0) || (texel_bytes == 0)) {
        return false;
    }
    if (
        (format != erhe::dataformat::Format::format_32_vec4_float) &&
        (format != erhe::dataformat::Format::format_16_vec4_float) &&
        (format != erhe::dataformat::Format::format_8_vec4_unorm)
    ) {
        return false;
    }
    const std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * texel_bytes;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);
    Buffer readback{
        graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap texture readback"}
        }
    };
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            &texture,
            0, 0,
            glm::ivec3{0, 0, 0},
            glm::ivec3{width, height, 1},
            &readback,
            0,
            bytes_per_row,
            byte_count
        );
    }
    command_buffer.transition_texture_layout(texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    graphics_device.wait_idle();

    const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
    out_data.resize(texel_count * 4u);
    switch (format) {
        case erhe::dataformat::Format::format_32_vec4_float: {
            std::memcpy(out_data.data(), mapped.data(), byte_count);
            break;
        }
        case erhe::dataformat::Format::format_16_vec4_float: {
            const auto* halves = reinterpret_cast<const uint16_t*>(mapped.data());
            for (std::size_t i = 0; i < texel_count * 4u; ++i) {
                out_data[i] = half_to_float(halves[i]);
            }
            break;
        }
        case erhe::dataformat::Format::format_8_vec4_unorm: {
            const auto* bytes = reinterpret_cast<const uint8_t*>(mapped.data());
            for (std::size_t i = 0; i < texel_count * 4u; ++i) {
                out_data[i] = static_cast<float>(bytes[i]) / 255.0f;
            }
            break;
        }
        default: {
            break; // unreachable - filtered above
        }
    }
    readback.unmap();
    return true;
}

// The lightmap G-buffer vertex shader positions triangles by their
// channel-2 UVs mapped into the region's atlas rect. Which NDC y lands in
// texture row 0 differs per backend (see Texture_renderer's Y_SIGN note);
// ERHE_LM_Y_SIGN keeps "row 0 is v = 0" everywhere.
constexpr const char* c_vertex_source = R"GLSL(
layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;
layout(location = 2) out vec3 v_albedo;
layout(location = 3) out vec3 v_nn_col0;
layout(location = 4) out vec3 v_nn_col1;
layout(location = 5) out vec3 v_nn_col2;
layout(location = 6) out vec3 v_nb;

void main()
{
    vec2 atlas_uv = a_texcoord_2 * lightmap_draw.uv_scale_offset.xy + lightmap_draw.uv_scale_offset.zw;
    vec2 ndc      = atlas_uv * 2.0 - 1.0 + lightmap_draw.jitter_ndc.xy;
    gl_Position   = vec4(ndc.x, ERHE_LM_Y_SIGN * ndc.y, 0.0, 1.0);
    vec3 world_position = (lightmap_draw.world_from_node * vec4(a_position, 1.0)).xyz;
    vec3 world_normal   = normalize(mat3(lightmap_draw.world_from_node) * a_normal);
    v_position    = world_position;
    v_normal      = world_normal;
    // Phong-tessellation smooth position (article terminator fix), split
    // into linearly interpolable parts: with barycentrics w_i,
    //   smooth(p) = p - sum_i w_i * dot(p - p_i, n_i) * n_i
    //             = p - (M(p) * p - b(p))
    // where M = sum_i w_i * n_i n_i^T and b = sum_i w_i * dot(p_i, n_i) n_i
    // are plain varyings (per-vertex n n^T columns and dot(p, n) n), and the
    // quadratic term comes from applying interpolated M to interpolated p in
    // the fragment stage.
    v_nn_col0     = world_normal * world_normal.x;
    v_nn_col1     = world_normal * world_normal.y;
    v_nn_col2     = world_normal * world_normal.z;
    v_nb          = dot(world_position, world_normal) * world_normal;
    // Read in the VERTEX stage and passed down as a varying: the per-draw
    // UBO binding is declared vertex-stage; a fragment-stage read is
    // undefined (observed live as base_color aliasing world_from_node
    // column 1 - every unrotated draw baked albedo (0,1,0), tinting all
    // bounce light green).
    v_albedo      = lightmap_draw.base_color.rgb;
}
)GLSL";

constexpr const char* c_fragment_source = R"GLSL(
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec3 v_nn_col0;
layout(location = 4) in vec3 v_nn_col1;
layout(location = 5) in vec3 v_nn_col2;
layout(location = 6) in vec3 v_nb;

void main()
{
    out_position = vec4(v_position, 1.0); // w = coverage
    // World-space texel size via derivatives (article): the raster maps one
    // atlas texel per pixel, so screen-space derivatives of world position
    // are meters-per-texel; the sqrt(2) covers the diagonal. Rides in
    // normal.w (was a redundant coverage copy) - the gather's virtual
    // offset probes use it as ray length.
    vec3  dpdx       = dFdx(v_position);
    vec3  dpdy       = dFdy(v_position);
    float texel_size = max(length(dpdx), length(dpdy)) * 1.41421356;
    out_normal   = vec4(normalize(v_normal), texel_size);
    // Diffuse albedo (base color factor x (1 - metallic); textures ignored
    // for now): modulates bounce radiance and later guides JNLM.
    out_albedo   = vec4(v_albedo, 1.0);
    // Smooth (Phong-tessellated) position: shadow/bounce ray origins start
    // here instead of the flat surface so the terminator matches the curved
    // surface the vertex normals imply (see the varying derivation in the
    // vertex shader). Validated against the FACE plane right away - the
    // derivatives above span the triangle, so their cross product is the
    // face normal (oriented by the smooth normal) - because a smooth
    // position below the face would start rays inside the surface. The
    // remaining validation (smooth position inside NEIGHBOR geometry) needs
    // rays and runs in the one-shot adjust pass.
    vec3 mp          = v_nn_col0 * v_position.x + v_nn_col1 * v_position.y + v_nn_col2 * v_position.z;
    vec3 smooth_pos  = v_position - (mp - v_nb);
    vec3 face_normal = cross(dpdx, dpdy);
    face_normal      = (dot(face_normal, v_normal) < 0.0) ? -face_normal : face_normal;
    if (dot(smooth_pos - v_position, face_normal) < 0.0) {
        smooth_pos = v_position;
    }
    out_smooth_position = vec4(smooth_pos, 1.0);
}
)GLSL";

// Supersample-origin raster (Frostbite Flux texel supersampling, slide
// "Texel sampling (2)"): the same UV-space raster as the G-buffer, but at
// c_supersample_factor x resolution and WITHOUT conservative raster or
// jitter - each covered hi-res texel center is a true on-triangle point of
// the regular sub-texel grid, so attributes are interpolated, never
// extrapolated. Output is the ray origin: the face-plane-validated
// Phong-tessellated smooth position (same math as the G-buffer fragment
// shader; terminator fix), or the flat position in the no-smooth variant.
// w = coverage (a zero-cleared hi-res texel is not on any triangle).
constexpr const char* c_origin_fragment_source = R"GLSL(
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;
layout(location = 2) in vec3 v_albedo;
layout(location = 3) in vec3 v_nn_col0;
layout(location = 4) in vec3 v_nn_col1;
layout(location = 5) in vec3 v_nn_col2;
layout(location = 6) in vec3 v_nb;

void main()
{
    vec3 origin = v_position;
#if !defined(ERHE_LM_NO_SMOOTH)
    vec3 mp          = v_nn_col0 * v_position.x + v_nn_col1 * v_position.y + v_nn_col2 * v_position.z;
    vec3 smooth_pos  = v_position - (mp - v_nb);
    vec3 face_normal = cross(dFdx(v_position), dFdy(v_position));
    face_normal      = (dot(face_normal, v_normal) < 0.0) ? -face_normal : face_normal;
    if (dot(smooth_pos - v_position, face_normal) >= 0.0) {
        origin = smooth_pos;
    }
#endif
    out_origin = vec4(origin, 1.0);
}
)GLSL";

constexpr std::size_t c_max_gather_lights = 16;

// Accumulating gather (plan section 3a): one sample per valid texel per
// dispatch - direct light via explicit sampling with a ray-query shadow
// ray per light, plus ONE cosine-weighted hemisphere bounce ray whose
// radiance comes from the PUBLISHED atlas at the hit point's lightmap UV
// (iterate-on-previous-lightmap). i_accum rgb holds the radiance sum, w
// the sample count; the resolve pass divides. Bind-group declarations
// (samplers, storage image, uniform block, instance SSBO) are injected by
// erhe; the acceleration structure is not (matching ray_trace.comp).
constexpr const char* c_gather_source = R"GLSL(
#include "sky_atmosphere_common.glsl"

layout(binding = 2) uniform accelerationStructureEXT s_tlas;

// Raw uint view of a mesh memory pool via per-instance buffer device
// addresses (same mechanism as ray_trace.comp; addresses pre-offset to the
// instance's index / stream-1 vertex range starts).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Uint_data {
    uint data[];
};

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

vec2 fetch_vec2(Uint_data vertices, uint base)
{
    return vec2(uintBitsToFloat(vertices.data[base + 0u]), uintBitsToFloat(vertices.data[base + 1u]));
}

vec3 fetch_vec3(Uint_data vertices, uint base)
{
    return vec3(
        uintBitsToFloat(vertices.data[base + 0u]),
        uintBitsToFloat(vertices.data[base + 1u]),
        uintBitsToFloat(vertices.data[base + 2u])
    );
}

// PCG (www.pcg-random.org): per-texel per-frame decorrelation.
uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float rand_float(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed) * (1.0 / 4294967296.0);
}

// Procedural sky radiance along dir: the sky_atmosphere.frag march
// (Hillaire EGSR 2020) against the same transmittance / multi-scatter
// LUTs the viewport sky uses, from the same fixed virtual observer. Two
// deliberate differences: the SUN DISC is excluded (the sun is a scene
// directional light, sampled by the direct shadow rays - the disc here
// would double-count it), and there is no exposure factor (the bake is
// plain HDR irradiance).
vec3 sky_sample_transmittance(float r, float mu)
{
    return textureLod(s_sky_transmittance, sky_transmittance_params_to_uv(r, mu), 0.0).rgb;
}

vec3 sky_sample_multiscatter(float r, float mu_sun)
{
    return textureLod(s_sky_multiscatter, sky_multiscatter_params_to_uv(r, mu_sun), 0.0).rgb;
}

vec3 sky_radiance(vec3 ray_dir)
{
    vec3  sun_dir         = normalize(lightmap_gather.sun_direction_and_intensity.xyz);
    float sun_illuminance = lightmap_gather.sun_direction_and_intensity.w;
    int   num_steps       = max(2, int(lightmap_gather.sky_params.x));
    vec3  ray_origin      = vec3(0.0, R_GROUND + lightmap_gather.sky_params.y, 0.0);

    Sky_ray_hit ground_hit = sky_ray_sphere(ray_origin, ray_dir, R_GROUND);
    bool        hit_ground = ground_hit.hit && (ground_hit.t_near > 0.0);
    float       t_top      = sky_distance_to_atmosphere_top(ray_origin, ray_dir);
    float       t_max      = hit_ground ? ground_hit.t_near : t_top;

    float cos_theta      = dot(ray_dir, sun_dir);
    float rayleigh_phase = sky_rayleigh_phase(cos_theta);
    float mie_phase      = sky_mie_phase_hg(cos_theta, MIE_G);

    vec3  inscatter  = vec3(0.0);
    vec3  throughput = vec3(1.0);
    float dt = t_max / float(num_steps);
    for (int s = 0; s < num_steps; ++s) {
        float t = (float(s) + 0.5) * dt;
        vec3  p = ray_origin + ray_dir * t;
        float r = length(p);

        Sky_medium m = sky_sample_medium(p);
        vec3 step_trans = exp(-m.extinction * dt);

        float mu_s      = dot(normalize(p), sun_dir);
        vec3  trans_sun = sky_sample_transmittance(r, mu_s);

        float shadow       = sky_hits_ground(p, sun_dir) ? 0.0 : 1.0;
        float horizon_fade = clamp(mu_s * 10.0 + 0.5, 0.0, 1.0);
        shadow *= horizon_fade;

        vec3 single = (m.rayleigh_scatter * rayleigh_phase + m.mie_scatter * mie_phase) * trans_sun * shadow;
        vec3 multi  = (m.rayleigh_scatter + m.mie_scatter) * sky_sample_multiscatter(r, mu_s);
        vec3 S      = (single + multi) * sun_illuminance;

        vec3 s_int = (S - S * step_trans) / m.extinction;
        inscatter += throughput * s_int;
        throughput *= step_trans;
    }

    // Lambertian planet-ground bounce for below-horizon rays that escape
    // the scene geometry (matches the rendered sky background).
    if (hit_ground) {
        vec3  ground_pos    = ray_origin + ray_dir * t_max;
        vec3  ground_normal = normalize(ground_pos);
        float sun_cos       = max(0.0, dot(ground_normal, sun_dir));
        vec3  trans_sun_g   = sky_sample_transmittance(R_GROUND, dot(ground_normal, sun_dir));
        inscatter += throughput * GROUND_ALBEDO * (sun_cos / SKY_PI) * trans_sun_g * sun_illuminance;
    }

    return inscatter;
}

// Adaptive self-intersection bias (article): offset each component
// proportionally to its magnitude (~FLT_EPSILON scale) along the given
// direction's signs, floored to a micron near the origin. Replaces the
// old fixed 1 cm normal offset, whose size itself caused the contact
// leak the no-cull hack papered over.
vec3 adaptive_offset(vec3 position, vec3 direction)
{
    vec3 magnitude = max(abs(position) * 2.0e-7, vec3(1.0e-6));
    return position + sign(direction) * magnitude;
}

// World-space geometric normal of the committed hit's triangle. Object-space
// positions come from the acceleration structure when the backend supports
// GL_EXT_ray_tracing_position_fetch, otherwise from the stream-0 pool (the
// BLAS build input, so the values are identical) via the instance record's
// position_address.
vec3 committed_face_normal(rayQueryEXT ray_query)
{
    vec3 positions[3];
#if ERHE_RT_HAS_POSITION_FETCH
    rayQueryGetIntersectionTriangleVertexPositionsEXT(ray_query, true, positions);
#else
    uint instance_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(ray_query, true));
    uint primitive      = uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray_query, true));
    Lm_instance_record record = lm_instance.instances[instance_index];
    Uint_data indices       = Uint_data(record.index_address);
    Uint_data position_data = Uint_data(record.position_address);
    uint i0 = indices.data[3u * primitive + 0u];
    uint i1 = indices.data[3u * primitive + 1u];
    uint i2 = indices.data[3u * primitive + 2u];
    positions[0] = fetch_vec3(position_data, i0 * record.position_stride_uints);
    positions[1] = fetch_vec3(position_data, i1 * record.position_stride_uints);
    positions[2] = fetch_vec3(position_data, i2 * record.position_stride_uints);
#endif
    mat4x3 world_from_object = rayQueryGetIntersectionObjectToWorldEXT(ray_query, true);
    vec3 p0 = world_from_object * vec4(positions[0], 1.0);
    vec3 p1 = world_from_object * vec4(positions[1], 1.0);
    vec3 p2 = world_from_object * vec4(positions[2], 1.0);
    return normalize(cross(p1 - p0, p2 - p0));
}

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy) + ivec2(0, int(lightmap_gather.base_texel_y));
    ivec2 size  = imageSize(i_accum);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 position_coverage = texelFetch(s_position, texel, 0);
    if (position_coverage.w <= 0.0) {
        imageStore(i_accum, texel, vec4(0.0));
        return;
    }
    // Position pre-adjusted by the one-shot virtual-offset pass
    // (c_adjust_source), so origins here are guaranteed outside geometry.
    vec3  p                = position_coverage.xyz;
    vec4  normal_and_size  = texelFetch(s_normal, texel, 0);
    vec3  n                = normalize(normal_and_size.xyz);
    float texel_size       = normal_and_size.w;

#if ERHE_LM_SUPERSAMPLE > 0
    // Texel supersampling (Frostbite Flux, slide "Texel sampling (2)"):
    // collect the texel's valid sub-texel sample points (regular
    // ERHE_LM_SUPERSAMPLE^2 grid, rasterized WITHOUT conservative raster,
    // so every covered point lies exactly on a triangle) and start each
    // ray from a uniform-randomly picked entry instead of the one fixed
    // per-texel origin. The list points skip the virtual-offset / interior
    // defenses that protect p - they are raster-exact on-surface positions,
    // covered by the adaptive self-intersection bias plus the existing
    // backface invalidation. Texels no grid point covers (the slide-1
    // pathology conservative raster still catches) fall back to p.
    vec3 ss_origins[ERHE_LM_SUPERSAMPLE * ERHE_LM_SUPERSAMPLE];
    int  ss_count = 0;
    for (int sy = 0; sy < ERHE_LM_SUPERSAMPLE; ++sy) {
        for (int sx = 0; sx < ERHE_LM_SUPERSAMPLE; ++sx) {
            vec4 ss_sample = texelFetch(s_origin, texel * ERHE_LM_SUPERSAMPLE + ivec2(sx, sy), 0);
            if (ss_sample.w > 0.0) {
                ss_origins[ss_count] = ss_sample.xyz;
                ++ss_count;
            }
        }
    }
    uint ss_seed = pcg_hash(uint(texel.x) * 7919u + uint(texel.y) * 104729u + lightmap_gather.frame_index * 15486277u + 1u);
#define ERHE_LM_RAY_ORIGIN ((ss_count > 0) ? ss_origins[min(int(rand_float(ss_seed) * float(ss_count)), ss_count - 1)] : p)
#else
#define ERHE_LM_RAY_ORIGIN p
#endif

    vec3 direct = vec3(0.0);
    for (uint i = 0u; i < lightmap_gather.light_count; ++i) {
        vec4 pos_type  = lightmap_gather.light_position_and_type[i];
        vec4 dir_cos   = lightmap_gather.light_direction_and_outer_cos[i];
        vec4 rad_range = lightmap_gather.light_radiance_and_range[i];
        vec4 params    = lightmap_gather.light_params[i];

        // Per-ray origin: with supersampling a random valid sub-texel
        // point, otherwise the per-texel position.
        vec3 ray_p = ERHE_LM_RAY_ORIGIN;

        vec3  to_light;
        float attenuation = 1.0;
        float t_max       = 1.0e30;
        if (pos_type.w < 0.5) { // directional
            to_light = normalize(dir_cos.xyz);
        } else {
            vec3 d = pos_type.xyz - ray_p;
            float distance = length(d);
            if (distance < 1.0e-6) {
                continue;
            }
            to_light    = d / distance;
            attenuation = 1.0 / max(distance * distance, 1.0e-4);
            t_max       = distance - 1.0e-3;
            if (pos_type.w > 1.5) { // spot: params.x = inner cos, dir_cos.w = outer cos
                float cos_angle = dot(to_light, normalize(dir_cos.xyz));
                attenuation *= smoothstep(dir_cos.w, params.x, cos_angle);
            }
        }
        float n_dot_l = dot(n, to_light);
        if ((n_dot_l <= 0.0) || (attenuation <= 0.0)) {
            continue;
        }
        // Backface culling restored (article defenses): the virtual offset
        // above guarantees the origin is OUTSIDE nearby geometry, and the
        // adaptive bias is far too small to jump a contact gap - the two
        // conditions the old no-cull hack existed to survive. Culling makes
        // coplanar self-hits impossible by construction.
        vec3 origin = adaptive_offset(ray_p, n);
        rayQueryEXT ray_query;
        rayQueryInitializeEXT(
            ray_query,
            s_tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT,
            0xFFu,
            origin,
            1.0e-4,
            to_light,
            t_max
        );
        rayQueryProceedEXT(ray_query);
        if (rayQueryGetIntersectionTypeEXT(ray_query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
            direct += rad_range.xyz * (n_dot_l * attenuation);
        }
    }
#if defined(ERHE_LM_DEBUG_GATHER)
    // Diagnostics: R = closest blocking hit t over all light rays (0 when
    // every ray reaches its light), G = max NdotL over lights, B = shadow
    // miss ratio (1 = all rays reached their light). Rays here mirror the
    // main loop exactly, including backface culling.
    {
        float ndotl_max = 0.0;
        float misses    = 0.0;
        float rays      = 0.0;
        float min_hit_t = 0.0;
        for (uint i = 0u; i < lightmap_gather.light_count; ++i) {
            vec3 to_light = normalize(lightmap_gather.light_direction_and_outer_cos[i].xyz);
            ndotl_max = max(ndotl_max, dot(n, to_light));
            rays += 1.0;
            rayQueryEXT rq2;
            rayQueryInitializeEXT(rq2, s_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsCullBackFacingTrianglesEXT, 0xFFu, adaptive_offset(p, n), 1.0e-4, to_light, 1.0e30);
            rayQueryProceedEXT(rq2);
            if (rayQueryGetIntersectionTypeEXT(rq2, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
                misses += 1.0;
            } else {
                float t = rayQueryGetIntersectionTEXT(rq2, true);
                min_hit_t = (min_hit_t == 0.0) ? t : min(min_hit_t, t);
            }
        }
        imageStore(i_accum, texel, vec4(min_hit_t, ndotl_max, (rays > 0.0) ? misses / rays : 0.0, 1.0));
        return;
    }
#endif

    // One cosine-weighted hemisphere ray, shared by the diffuse bounce and
    // the procedural sky. With cosine sampling the irradiance estimator is
    // E = pi * avg(L_in); for a diffuse hit the outgoing radiance is
    // albedo * E_hit / pi - the pi cancels, so each hit sample adds
    // albedo_hit * E_hit_published. A MISS escapes to the sky: L_in is the
    // atmosphere radiance, so the sample adds pi * sky_radiance(dir) (no
    // cancellation - the environment is a radiance source, not a diffuse
    // reflector). Hits on non-lightmapped instances (uv_scale_offset.x ==
    // 0) and interior/backface hits contribute nothing.
    // (ERHE_LM_NO_INDIRECT disables the ray entirely, sky included.)
    vec3 indirect = vec3(0.0);
#if !defined(ERHE_LM_NO_INDIRECT)
    if ((lightmap_gather.bounce_enabled != 0u) || (lightmap_gather.sky_enabled != 0u)) {
        uint  seed = pcg_hash(uint(texel.x) * 1973u + uint(texel.y) * 9277u + lightmap_gather.frame_index * 26699u);
        float u1   = rand_float(seed);
        float u2   = rand_float(seed);
        float r    = sqrt(u1);
        float phi  = 6.28318530718 * u2;
        vec3  t0   = normalize((abs(n.x) > 0.7) ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
        vec3  t1   = cross(n, t0);
        vec3  bounce_dir = normalize(t0 * (r * cos(phi)) + t1 * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));

        vec3 bounce_p = ERHE_LM_RAY_ORIGIN;
        rayQueryEXT bounce_query;
        rayQueryInitializeEXT(bounce_query, s_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, adaptive_offset(bounce_p, n), 1.0e-4, bounce_dir, 1.0e30);
        while (rayQueryProceedEXT(bounce_query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(bounce_query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
            vec3  face_normal = committed_face_normal(bounce_query);
            float hit_t       = rayQueryGetIntersectionTEXT(bounce_query, true);
            if (dot(face_normal, bounce_dir) > 0.0) {
                // Backface hit: the ray is looking at geometry interior. A
                // hit within a texel means the sample itself straddles
                // geometry the push-off could not fix - invalidate the
                // texel (article backface invalidation); dilation fills it
                // from valid neighbors. Farther backface hits (open meshes
                // seen from behind) just contribute nothing.
                if (hit_t < texel_size) {
                    imageStore(i_accum, texel, vec4(0.0));
                    return;
                }
            } else if (lightmap_gather.bounce_enabled != 0u) {
                uint instance_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(bounce_query, true));
                Lm_instance_record record = lm_instance.instances[instance_index];
                if (record.uv_scale_offset.x > 0.0) {
                    uint primitive    = uint(rayQueryGetIntersectionPrimitiveIndexEXT(bounce_query, true));
                    vec2 barycentrics = rayQueryGetIntersectionBarycentricsEXT(bounce_query, true);
                    Uint_data indices  = Uint_data(record.index_address);
                    Uint_data vertices = Uint_data(record.vertex_address);
                    uint i0 = indices.data[3u * primitive + 0u];
                    uint i1 = indices.data[3u * primitive + 1u];
                    uint i2 = indices.data[3u * primitive + 2u];
                    float w1 = barycentrics.x;
                    float w2 = barycentrics.y;
                    float w0 = 1.0 - w1 - w2;
                    vec2 uv2 =
                        w0 * fetch_vec2(vertices, i0 * record.vertex_stride_uints + uint(ERHE_LM_TEXCOORD2_OFFSET)) +
                        w1 * fetch_vec2(vertices, i1 * record.vertex_stride_uints + uint(ERHE_LM_TEXCOORD2_OFFSET)) +
                        w2 * fetch_vec2(vertices, i2 * record.vertex_stride_uints + uint(ERHE_LM_TEXCOORD2_OFFSET));
                    vec2 atlas_uv = uv2 * record.uv_scale_offset.xy + record.uv_scale_offset.zw;
                    vec3 hit_irradiance = textureLod(s_published, atlas_uv, 0.0).rgb;
                    vec3 hit_albedo     = textureLod(s_albedo,    atlas_uv, 0.0).rgb;
                    indirect = hit_albedo * hit_irradiance;
                }
            }
        } else if (lightmap_gather.sky_enabled != 0u) {
            // Escaped the scene: environment lighting from the sky.
            indirect = SKY_PI * sky_radiance(bounce_dir);
        }
    }
#endif

    vec4 accum = imageLoad(i_accum, texel);
    imageStore(i_accum, texel, accum + vec4(direct + indirect, 1.0));
}
)GLSL";

// Dilation (plan phase 4): invalid texels (w == 0) take the average of
// their valid 8-neighbors and become valid, so repeated passes grow every
// chart outward one texel at a time. Valid texels pass through unchanged.
// i_src / i_dst are declared by the bind group layout.
constexpr const char* c_dilate_source = R"GLSL(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_dst);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 value = imageLoad(i_src, texel);
    if (value.w > 0.0) {
        imageStore(i_dst, texel, value);
        return;
    }
    vec3  sum   = vec3(0.0);
    float count = 0.0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx == 0) && (dy == 0)) {
                continue;
            }
            ivec2 neighbor = texel + ivec2(dx, dy);
            if ((neighbor.x < 0) || (neighbor.y < 0) || (neighbor.x >= size.x) || (neighbor.y >= size.y)) {
                continue;
            }
            vec4 neighbor_value = imageLoad(i_src, neighbor);
            if (neighbor_value.w > 0.0) {
                sum   += neighbor_value.rgb;
                count += 1.0;
            }
        }
    }
    imageStore(i_dst, texel, (count > 0.0) ? vec4(sum / count, 1.0) : vec4(0.0));
}
)GLSL";

// Sample-position adjustment, run ONCE per G-buffer bake, before any
// gathering. Two article defenses in sequence, writing the position
// G-buffer in place:
// 1. Terminator fix: promote the ray origin to the Phong-tessellated
//    smooth position (already validated against the face plane at raster
//    time) unless the flat->smooth segment is blocked by neighbor
//    geometry - then keep the flat position.
// 2. Virtual offset: a texel center that sits inside nearby geometry
//    (conservative raster + bilinear footprints straddle contacts) leaks.
//    Probe 4 tangential rays half a texel long; the CLOSEST backface hit
//    means the sample is interior - move it just past that surface.
constexpr const char* c_adjust_source = R"GLSL(
layout(binding = 0) uniform accelerationStructureEXT s_tlas;

#if !ERHE_RT_HAS_POSITION_FETCH
// Raw uint view of a mesh memory pool via per-instance buffer device
// addresses; the position-fetch fallback in committed_face_normal reads
// triangle positions from the stream-0 pool with it (the lm_instance
// record SSBO is injected by erhe like in the gather shader).
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Uint_data {
    uint data[];
};

vec3 fetch_vec3(Uint_data vertices, uint base)
{
    return vec3(
        uintBitsToFloat(vertices.data[base + 0u]),
        uintBitsToFloat(vertices.data[base + 1u]),
        uintBitsToFloat(vertices.data[base + 2u])
    );
}
#endif

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

vec3 adaptive_offset(vec3 position, vec3 direction)
{
    vec3 magnitude = max(abs(position) * 2.0e-7, vec3(1.0e-6));
    return position + sign(direction) * magnitude;
}

vec3 committed_face_normal(rayQueryEXT ray_query)
{
    vec3 positions[3];
#if ERHE_RT_HAS_POSITION_FETCH
    rayQueryGetIntersectionTriangleVertexPositionsEXT(ray_query, true, positions);
#else
    uint instance_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(ray_query, true));
    uint primitive      = uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray_query, true));
    Lm_instance_record record = lm_instance.instances[instance_index];
    Uint_data indices       = Uint_data(record.index_address);
    Uint_data position_data = Uint_data(record.position_address);
    uint i0 = indices.data[3u * primitive + 0u];
    uint i1 = indices.data[3u * primitive + 1u];
    uint i2 = indices.data[3u * primitive + 2u];
    positions[0] = fetch_vec3(position_data, i0 * record.position_stride_uints);
    positions[1] = fetch_vec3(position_data, i1 * record.position_stride_uints);
    positions[2] = fetch_vec3(position_data, i2 * record.position_stride_uints);
#endif
    mat4x3 world_from_object = rayQueryGetIntersectionObjectToWorldEXT(ray_query, true);
    vec3 p0 = world_from_object * vec4(positions[0], 1.0);
    vec3 p1 = world_from_object * vec4(positions[1], 1.0);
    vec3 p2 = world_from_object * vec4(positions[2], 1.0);
    return normalize(cross(p1 - p0, p2 - p0));
}

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_position);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 position_coverage = imageLoad(i_position, texel);
    if (position_coverage.w <= 0.0) {
        return;
    }
    vec4  normal_and_size = imageLoad(i_normal, texel);
    vec3  n               = normalize(normal_and_size.xyz);
    float texel_size      = normal_and_size.w;
    vec3  p               = position_coverage.xyz;

    // Terminator fix: adopt the smooth position unless the segment from the
    // flat position to it crosses neighbor geometry (any hit - a frontface
    // crossing enters something, a backface crossing means we already were
    // inside something the probes below handle from the flat side).
#if !defined(ERHE_LM_NO_SMOOTH)
    vec3  p_smooth = imageLoad(i_smooth_position, texel).xyz;
    vec3  to_smooth = p_smooth - p;
    float smooth_distance = length(to_smooth);
    if (smooth_distance > 1.0e-6) {
        vec3 dir = to_smooth / smooth_distance;
        rayQueryEXT smooth_query;
        rayQueryInitializeEXT(smooth_query, s_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, adaptive_offset(p, dir), 1.0e-4, dir, smooth_distance);
        rayQueryProceedEXT(smooth_query);
        if (rayQueryGetIntersectionTypeEXT(smooth_query, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
            p = p_smooth;
        }
    }
#endif

    vec3  probe_t0     = normalize((abs(n.x) > 0.7) ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
    vec3  probe_t1     = cross(n, probe_t0);
    float probe_length = 0.5 * texel_size;
    vec3  push_dir     = vec3(0.0);
    vec3  push_normal  = vec3(0.0);
    float push_t       = 1.0e30;
    bool  push_found   = false;
    for (int probe = 0; probe < 4; ++probe) {
        vec3 dir =
            (probe == 0) ?  probe_t0 :
            (probe == 1) ? -probe_t0 :
            (probe == 2) ?  probe_t1 : -probe_t1;
        rayQueryEXT probe_query;
        rayQueryInitializeEXT(probe_query, s_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, adaptive_offset(p, n), 0.0, dir, probe_length);
        while (rayQueryProceedEXT(probe_query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(probe_query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
            vec3  face_normal = committed_face_normal(probe_query);
            float t           = rayQueryGetIntersectionTEXT(probe_query, true);
            if ((dot(face_normal, dir) > 0.0) && (t < push_t)) {
                push_t      = t;
                push_dir    = dir;
                push_normal = face_normal;
                push_found  = true;
            }
        }
    }
    if (push_found) {
        // Past the backface, then a hair to its front side (the face
        // normal points along the ray for a backface hit).
        p += push_dir * push_t + push_normal * 1.0e-4;
    }

    // Interior invalidation (article defense the half-texel probes above
    // cannot provide): a texel deep inside ANOTHER closed mesh (e.g. a
    // cylinder-top texel buried in an intersecting dodecahedron) passes
    // the probes, and its gather shadow rays then exit that mesh through
    // culled backfaces and bake full light - which dilation spreads
    // outward. Closed geometry gives an exact test: if the closest hit
    // along the normal, with no culling, is a backface, the origin is
    // inside something. Zero the coverage so the gather skips the texel
    // and dilation refills it from the legitimately shadowed texels just
    // outside the intersection.
    rayQueryEXT interior_query;
    rayQueryInitializeEXT(interior_query, s_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, adaptive_offset(p, n), 1.0e-4, n, 1.0e30);
    while (rayQueryProceedEXT(interior_query)) {
    }
    if (rayQueryGetIntersectionTypeEXT(interior_query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
        if (dot(committed_face_normal(interior_query), n) > 0.0) {
            imageStore(i_position, texel, vec4(p, 0.0));
            return;
        }
    }

    // Unconditional: the terminator fix above may have moved p even when
    // no probe pushed it.
    imageStore(i_position, texel, vec4(p, position_coverage.w));
}
)GLSL";

// Resolve (plan section 3a step 3): publish the running average
// (sum / count) from the accumulation atlas into the display atlas the
// renderer samples. Shares the dilate bind group layout (i_src / i_dst).
constexpr const char* c_resolve_source = R"GLSL(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_dst);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 accum = imageLoad(i_src, texel);
    imageStore(i_dst, texel, (accum.w > 0.0) ? vec4(accum.rgb / accum.w, 1.0) : vec4(0.0));
}
)GLSL";

// JNLM denoise (plan phase 4, article alignment: denoise before seams).
// Port of Godot's lm_compute.glsl MODE_DENOISE joint non-local means
// (MIT; itself based on YoctoImageDenoiser, MIT, Copyright (c) 2020
// ManuelPrandini, with corrections from "Nonlinearly Weighted First-order
// Regression for Denoising Monte Carlo Renderings"). Guides: G-buffer
// albedo + normal; a zero-length normal or zero published alpha marks an
// invalid texel (unbaked or backface-invalidated) which passes through
// unweighted, mirroring Godot's normal/occlusion masks. Windows are
// compile-time defines, REDUCED from Godot's defaults (half search 10,
// half patch 3) because this runs per completed sweep inside the
// interactive loop, not once at bake end; a future CLI bake can raise
// them.
constexpr const char* c_denoise_source = R"GLSL(
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const int   HALF_PATCH_WINDOW  = ERHE_LM_DENOISE_HALF_PATCH;
const int   HALF_SEARCH_WINDOW = ERHE_LM_DENOISE_HALF_SEARCH;
const float SIGMA_SPATIAL      = 2.0;
const float SIGMA_LIGHT        = 0.1;
const float SIGMA_ALBEDO       = 1.0;
const float SIGMA_NORMAL       = 0.1;
const float FILTER_VALUE       = 10.0 * SIGMA_LIGHT;

const int   PATCH_WINDOW_DIMENSION        = HALF_PATCH_WINDOW * 2 + 1;
const int   PATCH_WINDOW_DIMENSION_SQUARE = PATCH_WINDOW_DIMENSION * PATCH_WINDOW_DIMENSION;
const float TWO_SIGMA_SPATIAL_SQUARE      = 2.0 * SIGMA_SPATIAL * SIGMA_SPATIAL;
const float TWO_SIGMA_LIGHT_SQUARE        = 2.0 * SIGMA_LIGHT * SIGMA_LIGHT;
const float TWO_SIGMA_ALBEDO_SQUARE       = 2.0 * SIGMA_ALBEDO * SIGMA_ALBEDO;
const float TWO_SIGMA_NORMAL_SQUARE       = 2.0 * SIGMA_NORMAL * SIGMA_NORMAL;
const float FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE = FILTER_VALUE * FILTER_VALUE * TWO_SIGMA_LIGHT_SQUARE;
const float EPSILON = 1.0e-6;

void main()
{
    ivec2 texel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size  = imageSize(i_dst);
    if ((texel.x >= size.x) || (texel.y >= size.y)) {
        return;
    }
    vec4 input_light  = imageLoad(i_src, texel);
    vec3 input_normal = imageLoad(i_normal, texel).xyz;
    if ((input_light.a <= 0.0) || (length(input_normal) < EPSILON)) {
        imageStore(i_dst, texel, input_light);
        return;
    }
    vec3  input_rgb    = input_light.rgb;
    vec3  input_albedo = imageLoad(i_albedo, texel).rgb;
    vec3  denoised_rgb = vec3(0.0);
    float sum_weights  = 0.0;
    for (int search_y = -HALF_SEARCH_WINDOW; search_y <= HALF_SEARCH_WINDOW; ++search_y) {
        for (int search_x = -HALF_SEARCH_WINDOW; search_x <= HALF_SEARCH_WINDOW; ++search_x) {
            ivec2 search_pos    = texel + ivec2(search_x, search_y);
            ivec2 clamped_pos   = clamp(search_pos, ivec2(0), size - 1);
            vec4  search_light  = imageLoad(i_src,    clamped_pos);
            vec3  search_albedo = imageLoad(i_albedo, clamped_pos).rgb;
            vec3  search_normal = imageLoad(i_normal, clamped_pos).xyz;
            float patch_square_dist = 0.0;
            for (int offset_y = -HALF_PATCH_WINDOW; offset_y <= HALF_PATCH_WINDOW; ++offset_y) {
                for (int offset_x = -HALF_PATCH_WINDOW; offset_x <= HALF_PATCH_WINDOW; ++offset_x) {
                    ivec2 offset_input_pos  = clamp(texel      + ivec2(offset_x, offset_y), ivec2(0), size - 1);
                    ivec2 offset_search_pos = clamp(search_pos + ivec2(offset_x, offset_y), ivec2(0), size - 1);
                    vec3  offset_delta_rgb  = imageLoad(i_src, offset_input_pos).rgb - imageLoad(i_src, offset_search_pos).rgb;
                    patch_square_dist += dot(offset_delta_rgb, offset_delta_rgb) - TWO_SIGMA_LIGHT_SQUARE;
                }
            }
            patch_square_dist = max(0.0, patch_square_dist / (3.0 * float(PATCH_WINDOW_DIMENSION_SQUARE)));

            float weight = 1.0;
            // Out-of-bounds or invalid (unbaked / invalidated) search texels
            // contribute nothing.
            weight *= step(0.0, float(search_pos.x)) * step(float(search_pos.x), float(size.x - 1));
            weight *= step(0.0, float(search_pos.y)) * step(float(search_pos.y), float(size.y - 1));
            weight *= step(EPSILON, length(search_normal));
            weight *= step(0.5, search_light.a);

            vec2 pixel_delta = vec2(float(search_x), float(search_y));
            weight *= exp(-dot(pixel_delta, pixel_delta) / TWO_SIGMA_SPATIAL_SQUARE);
            weight *= exp(-patch_square_dist / FILTER_SQUARE_TWO_SIGMA_LIGHT_SQUARE);

            vec3 albedo_delta = input_albedo - search_albedo;
            weight *= exp(-dot(albedo_delta, albedo_delta) / TWO_SIGMA_ALBEDO_SQUARE);

            vec3 normal_delta = input_normal - search_normal;
            weight *= exp(-dot(normal_delta, normal_delta) / TWO_SIGMA_NORMAL_SQUARE);

            denoised_rgb += weight * search_light.rgb;
            sum_weights  += weight;
        }
    }
    denoised_rgb = (sum_weights > EPSILON) ? denoised_rgb / sum_weights : input_rgb;
    imageStore(i_dst, texel, vec4(denoised_rgb, input_light.a));
}
)GLSL";

// Seam blend (article: standard per-publish step; reference Godot
// lm_blendseams MODE_LINES): vertices carry the seam edge's own atlas UV
// (rasterization target) and the opposite side's atlas UV (sample source);
// the fragment alpha-blends the opposite side's radiance at 0.5 over the
// seam texels, pulling both sides of every seam together. Simplifications
// vs Godot: single center pass, no depth-mask + jitter passes (dilation
// already guards the chart borders bilinear reads from).
constexpr const char* c_seam_vertex_source = R"GLSL(
layout(location = 0) out vec2 v_src_uv;

void main()
{
    vec2 ndc    = a_position * 2.0 - 1.0;
    gl_Position = vec4(ndc.x, ERHE_LM_Y_SIGN * ndc.y, 0.0, 1.0);
    v_src_uv    = a_texcoord_0;
}
)GLSL";

constexpr const char* c_seam_fragment_source = R"GLSL(
layout(location = 0) in vec2 v_src_uv;

void main()
{
    out_color = vec4(textureLod(s_seam_source, v_src_uv, 0.0).rgb, 0.5);
}
)GLSL";

// Standard alpha blend for the seam lines; destination alpha is preserved
// (it is the texel validity flag).
const erhe::graphics::Color_blend_state c_seam_blend_state{
    .enabled = true,
    .rgb = {
        .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
        .source_factor      = erhe::graphics::Blending_factor::src_alpha,
        .destination_factor = erhe::graphics::Blending_factor::one_minus_src_alpha
    },
    .alpha = {
        .equation_mode      = erhe::graphics::Blend_equation_mode::func_add,
        .source_factor      = erhe::graphics::Blending_factor::zero,
        .destination_factor = erhe::graphics::Blending_factor::one
    }
};

// FNV-1a over raw bytes; drives the change-driven invalidation hashes.
[[nodiscard]] auto fnv1a64(const void* data, std::size_t byte_count, uint64_t hash = 0xcbf29ce484222325ull) -> uint64_t
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < byte_count; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001b3ull;
    }
    return hash;
}

} // anonymous namespace

Lightmap_baker::Lightmap_baker(erhe::graphics::Device& graphics_device, erhe::scene_renderer::Mesh_memory& mesh_memory)
    : m_graphics_device{graphics_device}
    , m_mesh_memory    {mesh_memory}
{
    using namespace erhe::graphics;

    m_draw_block = std::make_unique<Shader_resource>(
        graphics_device,
        Shader_resource::Block_create_info{
            .name          = "lightmap_draw",
            .binding_point = 0,
            .type          = Shader_resource::Type::uniform_block
        }
    );
    m_draw_block_world_offset      = m_draw_block->add_mat4("world_from_node")->get_offset_in_parent();
    m_draw_block_uv_offset         = m_draw_block->add_vec4("uv_scale_offset")->get_offset_in_parent();
    m_draw_block_jitter_offset     = m_draw_block->add_vec4("jitter_ndc")->get_offset_in_parent();
    m_draw_block_base_color_offset = m_draw_block->add_vec4("base_color")->get_offset_in_parent();
    m_draw_block_size              = m_draw_block->get_size_bytes(Shader_resource::Layout::std140);

    m_bind_group_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::vertex
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap gbuffer layout"},
            .uses_texture_heap = false
        }
    );
    m_fragment_outputs = std::make_unique<Fragment_outputs>(
        std::initializer_list<Fragment_output>{
            Fragment_output{ .name = "out_position",        .type = Glsl_type::float_vec4, .location = 0 },
            Fragment_output{ .name = "out_normal",          .type = Glsl_type::float_vec4, .location = 1 },
            Fragment_output{ .name = "out_albedo",          .type = Glsl_type::float_vec4, .location = 2 },
            Fragment_output{ .name = "out_smooth_position", .type = Glsl_type::float_vec4, .location = 3 }
        }
    );

    const bool top_left =
        (graphics_device.get_info().coordinate_conventions.texture_origin == erhe::math::Texture_origin::top_left);

    Shader_stages_create_info shader_create_info{
        .name             = "lightmap_gbuffer",
        .defines          = {
            { "ERHE_LM_Y_SIGN", top_left ? "-1.0" : "1.0" }
        },
        .interface_blocks = { m_draw_block.get() },
        .fragment_outputs = m_fragment_outputs.get(),
        .vertex_format    = &mesh_memory.vertex_format_not_skinned,
        .shaders = {
            { Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
            { Shader_type::fragment_shader, std::string_view{c_fragment_source} }
        },
        .bind_group_layout = m_bind_group_layout.get()
    };
    Shader_stages_prototype prototype = build_shader_stages(graphics_device, shader_create_info);
    if (!prototype.is_valid()) {
        log_render->warn("Lightmap_baker: G-buffer shader failed to compile/link");
        return;
    }
    m_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(prototype));

    const erhe::scene_renderer::Vertex_input_entry& vertex_input_entry =
        mesh_memory.get_vertex_input_from_vertex_format(mesh_memory.vertex_format_not_skinned);

    Render_pipeline_create_info pipeline_create_info;
    pipeline_create_info.base.input_assembly                    = Input_assembly_state::triangle;
    // Charts are rasterized in UV space. The unwrap is orientation-
    // preserving, so valid triangles keep the mesh's CCW winding there -
    // but folded parametrization triangles (observed on curved shapes:
    // ~1.6% of the torus) arrive mirrored and would overwrite good texels
    // with positions from elsewhere on the surface (they bake black).
    // Culling the flipped winding drops exactly the folds; texels only a
    // fold covered stay invalid and are filled by dilation instead.
    pipeline_create_info.base.rasterization                     = Rasterization_state::cull_mode_back_cw.with_winding_flip_if(top_left);
    // Native conservative rasterization (article alignment item 1): every
    // texel a chart triangle touches gets a fragment in ONE pass; the
    // jitter re-render loops in bake_gbuffer() are the alternatives
    // (Bake_options::coverage_mode selects; conservative falls back to
    // 9-tap without the extension). Both pipeline variants are built up
    // front so the mode switches without a pipeline rebuild.
    m_conservative_supported = graphics_device.get_info().use_conservative_rasterization;
    pipeline_create_info.base.rasterization.conservative_enable = false;
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = m_bind_group_layout.get();
    pipeline_create_info.base.color_blend                       = &Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = m_shader_stages.get();
    pipeline_create_info.vertex_input                           = vertex_input_entry.vertex_input.get();
    pipeline_create_info.color_attachment_count                 = 4;
    pipeline_create_info.color_attachment_formats[0]            = c_position_format;
    pipeline_create_info.color_attachment_formats[1]            = c_normal_format;
    pipeline_create_info.color_attachment_formats[2]            = c_albedo_format;
    pipeline_create_info.color_attachment_formats[3]            = c_position_format; // smooth position
    for (int i = 0; i < 4; ++i) {
        pipeline_create_info.color_usage_before[i]              = Image_usage_flag_bit_mask::sampled;
        pipeline_create_info.color_usage_after[i]               = Image_usage_flag_bit_mask::sampled;
    }
    pipeline_create_info.sample_count                           = 1;

    m_pipeline = std::make_unique<Render_pipeline>(graphics_device, pipeline_create_info);
    if (!m_pipeline->is_valid()) {
        log_render->warn("Lightmap_baker: G-buffer pipeline is not valid");
        m_pipeline.reset();
    }
    if (m_conservative_supported) {
        pipeline_create_info.base.rasterization.conservative_enable = true;
        m_pipeline_conservative = std::make_unique<Render_pipeline>(graphics_device, pipeline_create_info);
        if (!m_pipeline_conservative->is_valid()) {
            log_render->warn("Lightmap_baker: conservative G-buffer pipeline is not valid - jitter fallback only");
            m_pipeline_conservative.reset();
            m_conservative_supported = false;
        }
        pipeline_create_info.base.rasterization.conservative_enable = false;
    }

    // Gather: ray query in compute, exactly the machinery Ray_trace_renderer
    // proves out. The bounce ray additionally needs buffer device addresses
    // (texcoord-2 fetch) and the committed triangle's positions (backface
    // rejection) - from the acceleration structure when position fetch is
    // supported, otherwise from the stream-0 pool via the instance records.
    // Absent ray query, the layout + G-buffer still work; only baking is
    // unavailable.
    if (!graphics_device.get_info().use_ray_query) {
        log_render->info("Lightmap_baker: ray query not available, lightmap gather disabled");
        return;
    }
    const bool use_position_fetch = graphics_device.get_info().use_ray_tracing_position_fetch;

    m_gather_block = std::make_unique<Shader_resource>(
        graphics_device,
        Shader_resource::Block_create_info{
            .name          = "lightmap_gather",
            .binding_point = 0,
            .type          = Shader_resource::Type::uniform_block
        }
    );
    m_gather_light_count_offset    = m_gather_block->add_uint ("light_count")->get_offset_in_parent();
    m_gather_ray_bias_offset       = m_gather_block->add_float("ray_bias")->get_offset_in_parent();
    m_gather_frame_index_offset    = m_gather_block->add_uint ("frame_index")->get_offset_in_parent();
    m_gather_base_y_offset         = m_gather_block->add_uint ("base_texel_y")->get_offset_in_parent();
    m_gather_bounce_enabled_offset = m_gather_block->add_uint ("bounce_enabled")->get_offset_in_parent();
    m_gather_sky_enabled_offset    = m_gather_block->add_uint ("sky_enabled")->get_offset_in_parent();
    m_gather_sun_direction_offset  = m_gather_block->add_vec4("sun_direction_and_intensity")->get_offset_in_parent();
    m_gather_sky_params_offset     = m_gather_block->add_vec4("sky_params")->get_offset_in_parent();
    m_gather_position_type_offset  = m_gather_block->add_vec4("light_position_and_type",       c_max_gather_lights)->get_offset_in_parent();
    m_gather_direction_cos_offset  = m_gather_block->add_vec4("light_direction_and_outer_cos", c_max_gather_lights)->get_offset_in_parent();
    m_gather_radiance_range_offset = m_gather_block->add_vec4("light_radiance_and_range",      c_max_gather_lights)->get_offset_in_parent();
    m_gather_params_offset         = m_gather_block->add_vec4("light_params",                  c_max_gather_lights)->get_offset_in_parent();
    m_gather_block_size            = m_gather_block->get_size_bytes(Shader_resource::Layout::std140);

    // Per-instance records (std430 SSBO) for the bounce ray's texcoord-2
    // fetch; layout verified against the C++ mirror below.
    m_lm_instance_struct = std::make_unique<Shader_resource>(graphics_device, "Lm_instance_record");
    const std::size_t off_index_address    = m_lm_instance_struct->add_uvec2("index_address"        )->get_offset_in_parent();
    const std::size_t off_vertex_address   = m_lm_instance_struct->add_uvec2("vertex_address"       )->get_offset_in_parent();
    const std::size_t off_position_address = m_lm_instance_struct->add_uvec2("position_address"     )->get_offset_in_parent();
    const std::size_t off_stride           = m_lm_instance_struct->add_uint ("vertex_stride_uints"  )->get_offset_in_parent();
    const std::size_t off_position_stride  = m_lm_instance_struct->add_uint ("position_stride_uints")->get_offset_in_parent();
    const std::size_t off_uv_scale_offset  = m_lm_instance_struct->add_vec4 ("uv_scale_offset"      )->get_offset_in_parent();
    ERHE_VERIFY(off_index_address    == offsetof(Lm_instance_record, index_address));
    ERHE_VERIFY(off_vertex_address   == offsetof(Lm_instance_record, vertex_address));
    ERHE_VERIFY(off_position_address == offsetof(Lm_instance_record, position_address));
    ERHE_VERIFY(off_stride           == offsetof(Lm_instance_record, vertex_stride_uints));
    ERHE_VERIFY(off_position_stride  == offsetof(Lm_instance_record, position_stride_uints));
    ERHE_VERIFY(off_uv_scale_offset  == offsetof(Lm_instance_record, uv_scale_offset));
    ERHE_VERIFY(m_lm_instance_struct->get_size_bytes() == sizeof(Lm_instance_record));
    m_lm_instance_block = std::make_unique<Shader_resource>(
        graphics_device,
        Shader_resource::Block_create_info{
            .name          = "lm_instance",
            .binding_point = 1,
            .type          = Shader_resource::Type::shader_storage_block,
            .readonly      = true
        }
    );
    m_lm_instance_block->add_struct("instances", m_lm_instance_struct.get(), Shader_resource::unsized_array);

    // Stream-1 texcoord-2 offset (in uints) for the bounce ray's manual
    // vertex fetch, derived from the vertex format so they stay in sync.
    const erhe::dataformat::Attribute_stream texcoord2 =
        mesh_memory.vertex_format_not_skinned.find_attribute(erhe::dataformat::Vertex_attribute_usage::tex_coord, 2);
    ERHE_VERIFY((texcoord2.attribute != nullptr) && (texcoord2.stream != nullptr));
    ERHE_VERIFY((texcoord2.attribute->offset % 4) == 0);

    m_gather_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::uniform_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 2u,
                    .type          = Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = Shader_stage_flags::compute
                },
                // Combined image samplers land at vk binding = user +
                // (max buffer binding + 1) = user + 2 here (buffers at 0/1):
                // s_position vk 5, s_normal vk 6, s_albedo vk 7,
                // s_published vk 8, s_sky_transmittance vk 9,
                // s_sky_multiscatter vk 10, s_origin vk 11. Raw bindings
                // (TLAS, storage image) are NOT offset, so the accumulation
                // image sits at 12, clear of every sampler's vk slot.
                Bind_group_layout_binding{
                    .binding_point   = 3u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_position",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 4u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_normal",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 5u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_albedo",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 6u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_published",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                // Procedural sky LUTs (Sky_renderer); bound to the G-buffer
                // albedo texture as an inert placeholder when the sky is off
                // or the LUTs do not exist (sky_enabled == 0 -> never
                // sampled, but the binding must hold a valid texture).
                Bind_group_layout_binding{
                    .binding_point   = 7u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_sky_transmittance",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point   = 8u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_sky_multiscatter",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                // Supersampled ray origins (c_supersample_factor x page);
                // bound to the G-buffer position texture as an inert
                // placeholder when supersampling is off (the non-SS gather
                // variant never samples it, but the binding must hold a
                // valid texture - same pattern as the sky LUTs).
                Bind_group_layout_binding{
                    .binding_point   = 9u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_origin",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 12u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_accum",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_accum_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap gather layout"},
            .uses_texture_heap = false
        }
    );

    // Both gather variants (single per-texel origin, and supersampled
    // ray origins per Bake_options::supersample) are compiled up front so
    // the option switches without a shader rebuild.
    const char* const no_indirect     = std::getenv("ERHE_LM_NO_INDIRECT");
    const bool        env_no_indirect = (no_indirect != nullptr) && (no_indirect[0] == '1');
    if (env_no_indirect) {
        log_render->warn("Lightmap_baker: ERHE_LM_NO_INDIRECT=1 - bounce ray disabled");
    }
    const auto make_gather = [&](
        const int                                           supersample_factor,
        std::unique_ptr<erhe::graphics::Shader_stages>&     out_shader_stages,
        std::unique_ptr<erhe::graphics::Compute_pipeline>&  out_pipeline
    ) -> bool {
        const char* const name =
            (supersample_factor == 8) ? "lightmap_gather_supersample_8x8" :
            (supersample_factor == 4) ? "lightmap_gather_supersample_4x4" : "lightmap_gather";
        Shader_stages_create_info gather_create_info{
            .name             = name,
            // Diagnostics: add { "ERHE_LM_DEBUG_GATHER", "1" } to defines to bake
            // R = closest blocking hit t, G = max NdotL, B = shadow miss ratio
            // instead of irradiance (see the debug block in c_gather_source).
            // Environment ERHE_LM_NO_INDIRECT=1 disables the bounce ray so the
            // atlas holds pure direct irradiance (isolates bounce defects).
            .defines          = [&]() {
                std::vector<std::pair<std::string, std::string>> defines{
                    { "ERHE_LM_TEXCOORD2_OFFSET",   fmt::format("{}", texcoord2.attribute->offset / 4) },
                    { "ERHE_RT_HAS_POSITION_FETCH", use_position_fetch ? "1" : "0" },
                    { "ERHE_LM_SUPERSAMPLE",        fmt::format("{}", supersample_factor) }
                };
                if (env_no_indirect) {
                    defines.push_back({ "ERHE_LM_NO_INDIRECT", "1" });
                }
                return defines;
            }(),
            .extensions       = [&]() {
                std::vector<Shader_stage_extension> extensions{
                    { Shader_type::compute_shader, "GL_EXT_ray_query" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
                    { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
                };
                if (use_position_fetch) {
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
                }
                return extensions;
            }(),
            .struct_types     = { m_lm_instance_struct.get() },
            .interface_blocks = { m_gather_block.get(), m_lm_instance_block.get() },
            .shaders = {
                { Shader_type::compute_shader, std::string_view{c_gather_source} }
            },
            // For #include "sky_atmosphere_common.glsl" (shared with the
            // viewport sky shaders - single source of truth for the math).
            .extra_include_paths = {
                std::filesystem::path{"res"} / std::filesystem::path{"editor"} / std::filesystem::path{"shaders"}
            },
            .bind_group_layout = m_gather_layout.get()
        };
        Shader_stages_prototype gather_prototype = build_shader_stages(graphics_device, gather_create_info);
        if (!gather_prototype.is_valid()) {
            log_render->warn("Lightmap_baker: {} shader failed to compile/link", name);
            return false;
        }
        out_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(gather_prototype));
        out_pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = name,
                .shader_stages     = out_shader_stages.get(),
                .bind_group_layout = m_gather_layout.get()
            }
        );
        return true;
    };
    if (!make_gather(0, m_gather_shader_stages, m_gather_pipeline)) {
        return;
    }
    // Supersampled variants are optional: on failure the plain gather
    // still works and Bake_options::supersample_factor has no effect.
    if (!make_gather(4, m_gather_ss4_shader_stages, m_gather_ss4_pipeline)) {
        m_gather_ss4_shader_stages.reset();
        m_gather_ss4_pipeline.reset();
    }
    if (!make_gather(8, m_gather_ss8_shader_stages, m_gather_ss8_pipeline)) {
        m_gather_ss8_shader_stages.reset();
        m_gather_ss8_pipeline.reset();
    }
    m_nearest_sampler = std::make_unique<Sampler>(
        graphics_device,
        Sampler_create_info{
            .min_filter  = Filter::nearest,
            .mag_filter  = Filter::nearest,
            .mipmap_mode = Sampler_mipmap_mode::not_mipmapped,
            .debug_label = "lightmap gather sampler"
        }
    );
    // Bilinear for the published-atlas and albedo lookups at bounce-ray hit
    // points (arbitrary positions inside charts; padding + dilation keep
    // the filter footprint valid).
    m_linear_sampler = std::make_unique<Sampler>(
        graphics_device,
        Sampler_create_info{
            .min_filter   = Filter::linear,
            .mag_filter   = Filter::linear,
            .mipmap_mode  = Sampler_mipmap_mode::not_mipmapped,
            .address_mode = { Sampler_address_mode::clamp_to_edge, Sampler_address_mode::clamp_to_edge, Sampler_address_mode::clamp_to_edge },
            .debug_label  = "lightmap bounce sampler"
        }
    );

    m_tick_gather_ubo    = std::make_unique<Ring_buffer_client>(graphics_device, Buffer_target::uniform, "Lightmap_baker::gather_ubo",       0u);
    m_tick_instance_ssbo = std::make_unique<Ring_buffer_client>(graphics_device, Buffer_target::storage, "Lightmap_baker::instance_records", 1u);

    // Virtual-offset adjust pass (one dispatch per G-buffer bake): TLAS +
    // instance records (the committed_face_normal position-fetch fallback
    // reads them; bound in both variants so the layout is uniform) +
    // read-write position + read-only normal, all raw bindings. The SSBO
    // sits at binding 1 because m_lm_instance_block's binding point is
    // fixed there (shared with the gather layout).
    m_adjust_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::acceleration_structure,
                    .name          = "s_tlas",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_buffer,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 2u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_position",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 3u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_normal",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_normal_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 4u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_smooth_position",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap adjust layout"},
            .uses_texture_heap = false
        }
    );
    // Both variants are compiled up front so Bake_options::terminator_fix
    // can switch without a shader rebuild. Environment ERHE_LM_NO_SMOOTH=1
    // still forces the smooth adoption off entirely (A/B diagnostics;
    // mirrors ERHE_LM_NO_INDIRECT) by compiling the "smooth" slot with the
    // define as well.
    const char* const no_smooth_env = std::getenv("ERHE_LM_NO_SMOOTH");
    const bool        env_no_smooth = (no_smooth_env != nullptr) && (no_smooth_env[0] == '1');
    if (env_no_smooth) {
        log_render->warn("Lightmap_baker: ERHE_LM_NO_SMOOTH=1 - terminator smooth position disabled");
    }
    const auto make_adjust = [&](
        const bool                                           with_smooth,
        std::unique_ptr<erhe::graphics::Shader_stages>&      out_shader_stages,
        std::unique_ptr<erhe::graphics::Compute_pipeline>&   out_pipeline
    ) -> bool {
        Shader_stages_create_info adjust_create_info{
            .name       = with_smooth ? "lightmap_adjust" : "lightmap_adjust_no_smooth",
            .defines    = [&]() {
                std::vector<std::pair<std::string, std::string>> defines{
                    { "ERHE_RT_HAS_POSITION_FETCH", use_position_fetch ? "1" : "0" }
                };
                if (!with_smooth) {
                    defines.push_back({ "ERHE_LM_NO_SMOOTH", "1" });
                }
                return defines;
            }(),
            .extensions = [&]() {
                std::vector<Shader_stage_extension> extensions{
                    { Shader_type::compute_shader, "GL_EXT_ray_query" }
                };
                if (use_position_fetch) {
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" });
                } else {
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_buffer_reference" });
                    extensions.push_back({ Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" });
                }
                return extensions;
            }(),
            .struct_types     = { m_lm_instance_struct.get() },
            .interface_blocks = { m_lm_instance_block.get() },
            .shaders = {
                { Shader_type::compute_shader, std::string_view{c_adjust_source} }
            },
            .bind_group_layout = m_adjust_layout.get()
        };
        Shader_stages_prototype adjust_prototype = build_shader_stages(graphics_device, adjust_create_info);
        if (!adjust_prototype.is_valid()) {
            log_render->warn("Lightmap_baker: adjust shader failed to compile/link");
            return false;
        }
        out_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(adjust_prototype));
        out_pipeline = std::make_unique<Compute_pipeline>(
            graphics_device,
            Compute_pipeline_data{
                .name              = with_smooth ? "lightmap_adjust" : "lightmap_adjust_no_smooth",
                .shader_stages     = out_shader_stages.get(),
                .bind_group_layout = m_adjust_layout.get()
            }
        );
        return true;
    };
    if (!make_adjust(!env_no_smooth, m_adjust_shader_stages, m_adjust_pipeline)) {
        return;
    }
    if (!make_adjust(false, m_adjust_no_smooth_shader_stages, m_adjust_no_smooth_pipeline)) {
        return;
    }

    // Supersample-origin raster pass (Bake_options::supersample_factor):
    // the G-buffer vertex shader with a single-output fragment shader,
    // drawn at supersample_factor x page resolution WITHOUT conservative
    // raster (factor-independent - only the render target size differs).
    // Smooth / no-smooth variants mirror the adjust pass (ERHE_LM_NO_SMOOTH
    // env forces no-smooth like everywhere else); optional - on failure the
    // supersample option simply has no effect.
    if (m_gather_ss4_pipeline || m_gather_ss8_pipeline) {
        m_origin_fragment_outputs = std::make_unique<Fragment_outputs>(
            std::initializer_list<Fragment_output>{
                Fragment_output{ .name = "out_origin", .type = Glsl_type::float_vec4, .location = 0 }
            }
        );
        const auto make_origin = [&](
            const bool                                        with_smooth,
            std::unique_ptr<erhe::graphics::Shader_stages>&   out_shader_stages,
            std::unique_ptr<erhe::graphics::Render_pipeline>& out_pipeline
        ) -> bool {
            const char* const name = with_smooth ? "lightmap_origins" : "lightmap_origins_no_smooth";
            Shader_stages_create_info origin_create_info{
                .name             = name,
                .defines          = [&]() {
                    std::vector<std::pair<std::string, std::string>> defines{
                        { "ERHE_LM_Y_SIGN", top_left ? "-1.0" : "1.0" }
                    };
                    if (!with_smooth) {
                        defines.push_back({ "ERHE_LM_NO_SMOOTH", "1" });
                    }
                    return defines;
                }(),
                .interface_blocks = { m_draw_block.get() },
                .fragment_outputs = m_origin_fragment_outputs.get(),
                .vertex_format    = &mesh_memory.vertex_format_not_skinned,
                .shaders = {
                    { Shader_type::vertex_shader,   std::string_view{c_vertex_source} },
                    { Shader_type::fragment_shader, std::string_view{c_origin_fragment_source} }
                },
                .bind_group_layout = m_bind_group_layout.get()
            };
            Shader_stages_prototype origin_prototype = build_shader_stages(graphics_device, origin_create_info);
            if (!origin_prototype.is_valid()) {
                log_render->warn("Lightmap_baker: {} shader failed to compile/link", name);
                return false;
            }
            out_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(origin_prototype));
            Render_pipeline_create_info origin_pipeline_create_info;
            origin_pipeline_create_info.base.input_assembly                    = Input_assembly_state::triangle;
            // Same fold-culling raster state as the G-buffer, but NEVER
            // conservative: valid sub-texel points must lie ON a triangle
            // (interpolated attributes), not merely touch one.
            origin_pipeline_create_info.base.rasterization                     = Rasterization_state::cull_mode_back_cw.with_winding_flip_if(top_left);
            origin_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
            origin_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
            origin_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
            origin_pipeline_create_info.base.bind_group_layout                 = m_bind_group_layout.get();
            origin_pipeline_create_info.base.color_blend                       = &Color_blend_state::color_blend_disabled;
            origin_pipeline_create_info.shader_stages                          = out_shader_stages.get();
            origin_pipeline_create_info.vertex_input                           = vertex_input_entry.vertex_input.get();
            origin_pipeline_create_info.color_attachment_count                 = 1;
            origin_pipeline_create_info.color_attachment_formats[0]            = c_position_format;
            origin_pipeline_create_info.color_usage_before[0]                  = Image_usage_flag_bit_mask::sampled;
            origin_pipeline_create_info.color_usage_after[0]                   = Image_usage_flag_bit_mask::sampled;
            origin_pipeline_create_info.sample_count                           = 1;
            out_pipeline = std::make_unique<Render_pipeline>(graphics_device, origin_pipeline_create_info);
            if (!out_pipeline->is_valid()) {
                log_render->warn("Lightmap_baker: {} pipeline is not valid", name);
                out_pipeline.reset();
                return false;
            }
            return true;
        };
        const bool origins_ok =
            make_origin(!env_no_smooth, m_origin_shader_stages,           m_origin_pipeline) &&
            make_origin(false,          m_origin_no_smooth_shader_stages, m_origin_no_smooth_pipeline);
        if (!origins_ok) {
            m_origin_pipeline.reset();
            m_origin_no_smooth_pipeline.reset();
            m_gather_ss4_shader_stages.reset();
            m_gather_ss4_pipeline.reset();
            m_gather_ss8_shader_stages.reset();
            m_gather_ss8_pipeline.reset();
        }
    }

    // Dilation pipeline. Both bindings are raw storage images, so the
    // combined-image-sampler binding offset (see the gather layout note)
    // does not apply.
    m_dilate_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_src",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_atlas_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_dst",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_atlas_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap dilate layout"},
            .uses_texture_heap = false
        }
    );
    Shader_stages_create_info dilate_create_info{
        .name    = "lightmap_dilate",
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_dilate_source} }
        },
        .bind_group_layout = m_dilate_layout.get()
    };
    Shader_stages_prototype dilate_prototype = build_shader_stages(graphics_device, dilate_create_info);
    if (!dilate_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: dilate shader failed to compile/link");
        return;
    }
    m_dilate_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(dilate_prototype));
    m_dilate_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_dilate",
            .shader_stages     = m_dilate_shader_stages.get(),
            .bind_group_layout = m_dilate_layout.get()
        }
    );

    // Resolve (accum running average -> published atlas); same binding
    // shape as dilate, but its own layout because the source is the fp32
    // accumulation target while the destination is the fp16 atlas.
    m_resolve_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_src",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_accum_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_dst",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_atlas_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap resolve layout"},
            .uses_texture_heap = false
        }
    );
    Shader_stages_create_info resolve_create_info{
        .name    = "lightmap_resolve",
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_resolve_source} }
        },
        .bind_group_layout = m_resolve_layout.get()
    };
    Shader_stages_prototype resolve_prototype = build_shader_stages(graphics_device, resolve_create_info);
    if (!resolve_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: resolve shader failed to compile/link");
        return;
    }
    m_resolve_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(resolve_prototype));
    m_resolve_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_resolve",
            .shader_stages     = m_resolve_shader_stages.get(),
            .bind_group_layout = m_resolve_layout.get()
        }
    );

    // JNLM denoise (all raw storage-image bindings, no sampler offset).
    m_denoise_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point = 0u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_src",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_atlas_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_dst",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_atlas_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 2u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_normal",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_normal_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 3u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_albedo",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = c_albedo_image_format,
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap denoise layout"},
            .uses_texture_heap = false
        }
    );
    Shader_stages_create_info denoise_create_info{
        .name    = "lightmap_denoise",
        .defines = {
            { "ERHE_LM_DENOISE_HALF_PATCH",  "1" },
            { "ERHE_LM_DENOISE_HALF_SEARCH", "3" }
        },
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_denoise_source} }
        },
        .bind_group_layout = m_denoise_layout.get()
    };
    Shader_stages_prototype denoise_prototype = build_shader_stages(graphics_device, denoise_create_info);
    if (!denoise_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: denoise shader failed to compile/link");
        return;
    }
    m_denoise_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(denoise_prototype));
    m_denoise_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_denoise",
            .shader_stages     = m_denoise_shader_stages.get(),
            .bind_group_layout = m_denoise_layout.get()
        }
    );

    // Seam blend pass: line raster over the published atlas.
    m_seam_vertex_format = erhe::dataformat::Vertex_format{
        {
            0,
            {
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::position,  0 },
                { erhe::dataformat::Format::format_32_vec2_float, erhe::dataformat::Vertex_attribute_usage::tex_coord, 0 }
            }
        }
    };
    m_seam_vertex_input = std::make_unique<Vertex_input_state>(
        graphics_device,
        Vertex_input_state_data::make(m_seam_vertex_format)
    );
    m_seam_layout = std::make_unique<Bind_group_layout>(
        graphics_device,
        Bind_group_layout_create_info{
            .bindings = {
                Bind_group_layout_binding{
                    .binding_point   = 0u,
                    .type            = Binding_type::combined_image_sampler,
                    .sampler_aspect  = Sampler_aspect::color,
                    .name            = "s_seam_source",
                    .glsl_type       = Glsl_type::sampler_2d,
                    .is_texture_heap = false,
                    .stage_flags     = Shader_stage_flags::fragment
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap seam layout"},
            .uses_texture_heap = false
        }
    );
    m_seam_fragment_outputs = std::make_unique<Fragment_outputs>(
        std::initializer_list<Fragment_output>{
            Fragment_output{ .name = "out_color", .type = Glsl_type::float_vec4, .location = 0 }
        }
    );
    Shader_stages_create_info seam_create_info{
        .name             = "lightmap_seam_blend",
        .defines          = {
            { "ERHE_LM_Y_SIGN", top_left ? "-1.0" : "1.0" }
        },
        .fragment_outputs = m_seam_fragment_outputs.get(),
        .vertex_format    = &m_seam_vertex_format,
        .shaders = {
            { Shader_type::vertex_shader,   std::string_view{c_seam_vertex_source} },
            { Shader_type::fragment_shader, std::string_view{c_seam_fragment_source} }
        },
        .bind_group_layout = m_seam_layout.get()
    };
    Shader_stages_prototype seam_prototype = build_shader_stages(graphics_device, seam_create_info);
    if (!seam_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: seam blend shader failed to compile/link");
        return;
    }
    m_seam_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(seam_prototype));

    Render_pipeline_create_info seam_pipeline_create_info;
    seam_pipeline_create_info.base.input_assembly                    = Input_assembly_state::line;
    seam_pipeline_create_info.base.rasterization                     = Rasterization_state::cull_mode_none;
    seam_pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    seam_pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    seam_pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    seam_pipeline_create_info.base.bind_group_layout                 = m_seam_layout.get();
    seam_pipeline_create_info.base.color_blend                       = &c_seam_blend_state;
    seam_pipeline_create_info.shader_stages                          = m_seam_shader_stages.get();
    seam_pipeline_create_info.vertex_input                           = m_seam_vertex_input.get();
    seam_pipeline_create_info.color_attachment_count                 = 1;
    seam_pipeline_create_info.color_attachment_formats[0]            = c_atlas_format;
    seam_pipeline_create_info.color_usage_before[0]                  = Image_usage_flag_bit_mask::storage;
    seam_pipeline_create_info.color_usage_after[0]                   = Image_usage_flag_bit_mask::storage;
    seam_pipeline_create_info.sample_count                           = 1;
    m_seam_pipeline = std::make_unique<Render_pipeline>(graphics_device, seam_pipeline_create_info);
    if (!m_seam_pipeline->is_valid()) {
        log_render->warn("Lightmap_baker: seam blend pipeline is not valid");
        m_seam_pipeline.reset();
    }
    m_seam_vertex_ring = std::make_unique<Ring_buffer_client>(graphics_device, Buffer_target::vertex, "Lightmap_baker::seam_vertices", 0u);
}

Lightmap_baker::~Lightmap_baker() noexcept = default;

auto Lightmap_baker::is_supported() const -> bool
{
    return static_cast<bool>(m_pipeline);
}

auto Lightmap_baker::is_bake_supported() const -> bool
{
    return static_cast<bool>(m_gather_pipeline);
}

auto Lightmap_baker::Atlas_layout::get_slot_origin(const int slot) const -> glm::ivec2
{
    const int sx = (slots_x > 0) ? (slot % slots_x) : 0;
    const int sy = (slots_x > 0) ? (slot / slots_x) : 0;
    return glm::ivec2{sx * tile_size, sy * tile_size};
}

auto Lightmap_baker::Atlas_layout::display_uv_scale_offset(const Instance_region& region) const -> glm::vec4
{
    if ((region.tile < 0) || (region.tile >= static_cast<int>(tiles.size())) || (width <= 0)) {
        return glm::vec4{0.0f};
    }
    const int slot = tiles[static_cast<std::size_t>(region.tile)].slot;
    if (slot < 0) {
        // Not resident. Ordinary regions publish the renderer's no-lightmap
        // gate (vec4(0)); world-space tile pieces publish the white-fallback
        // sentinel (scale.x < 0, see standard.frag) so every lightmapped
        // piece keeps rendering (flat white) until its tile loads.
        return partitioned ? glm::vec4{-1.0f, 0.0f, 0.0f, 0.0f} : glm::vec4{0.0f};
    }
    const glm::ivec2 slot_origin = get_slot_origin(slot);
    const float      inv_display = 1.0f / static_cast<float>(width);
    return glm::vec4{
        static_cast<float>(region.width)  * inv_display,
        static_cast<float>(region.height) * inv_display,
        static_cast<float>(slot_origin.x + region.x) * inv_display,
        static_cast<float>(slot_origin.y + region.y) * inv_display
    };
}

void Lightmap_baker::set_tile_config(const int tile_texture_size, const int resident_tile_budget)
{
    const int clamped = std::clamp(static_cast<int>(std::bit_ceil(static_cast<unsigned int>(std::max(1, tile_texture_size)))), s_min_tile, s_max_tile);
    m_tile_size   = clamped;
    m_slot_budget = std::max(1, resident_tile_budget);
}

void Lightmap_baker::set_cell_size(const float cell_size_m)
{
    m_cell_size = std::clamp(cell_size_m, 0.25f, 1024.0f);
}

void Lightmap_baker::set_tile_overrides(const std::vector<glm::ivec3>& overrides)
{
    m_tile_overrides = overrides;
}

auto Lightmap_baker::get_grid_parameters_hash() const -> uint64_t
{
    uint64_t hash = fnv1a64(&m_cell_size, sizeof(float));
    hash = fnv1a64(&m_tile_size, sizeof(int), hash);
    for (const glm::ivec3& value : m_tile_overrides) {
        hash = fnv1a64(&value, sizeof(glm::ivec3), hash);
    }
    return hash;
}

auto Lightmap_baker::get_sweep_count() const -> uint32_t
{
    // Minimum over the active, content-carrying tiles: every active valid
    // texel holds at least this many samples.
    uint32_t result = std::numeric_limits<uint32_t>::max();
    bool     any    = false;
    for (const Tile_state& tile : m_tiles) {
        if (!tile.active || !tile.has_content) {
            continue;
        }
        result = std::min(result, tile.sweeps);
        any    = true;
    }
    return any ? result : 0u;
}

auto Lightmap_baker::update_layout(Scene_root& scene_root, const float min_face_texels) -> bool
{
    m_layout            = Atlas_layout{};
    m_gbuffer_valid     = false;
    m_gbuffer_tile      = -1;
    m_lightmap_valid    = false;
    m_regions_published = false;
    m_tiles.clear();
    m_cursor_tile       = 0;
    m_cursor_y          = 0;
    // Regions move on a repack, so the previous publish no longer matches
    // the uv_scale_offsets pushed to the meshes; ensure_bake_targets clears
    // the display atlas when this is false.
    m_display_cleared   = false;
    m_slots_pending_white_clear.clear(); // slot indices belong to the old layout
    m_layout_scene_root = &scene_root;

    // The prepared world-space partition for this scene supplies the
    // regions (piece meshes with pre-assigned grid tiles) and the tile
    // tree. Without one there is no layout - Prepare World-Space Tiles is
    // the only front door (the legacy whole-mesh channel-2 layout was
    // removed).
    if ((m_partitioner == nullptr) || !m_partitioner->is_prepared() || (m_partitioner->get_scene_root() != &scene_root)) {
        log_render->info("Lightmap_baker::update_layout: no prepared world-space partition for this scene - run Prepare World-Space Tiles");
        m_seam_vertices.clear();
        return false;
    }
    return update_layout_partitioned(scene_root, min_face_texels);
}

auto Lightmap_baker::Grid_split::tile_for_position(const glm::vec2 xz, const float base_cell_size) const -> int
{
    // Finest level first so a subdivided child wins over the cell it split.
    int min_level = 0;
    int max_level = 0;
    for (const auto& [key, tile] : tile_of_key) {
        min_level = std::min(min_level, key.level);
        max_level = std::max(max_level, key.level);
    }
    for (int level = max_level; level >= min_level; --level) {
        const Lightmap_tile_key key = Lightmap_tile_key::for_position(base_cell_size, level, xz.x, xz.y);
        const auto it = tile_of_key.find(key);
        if (it != tile_of_key.end()) {
            return it->second;
        }
    }
    return -1;
}

auto Lightmap_baker::build_grid_split(const std::vector<erhe::math::Aabb>& region_bounds) -> Grid_split
{
    // ---- Uniform quadtree grid (world-origin anchored) ----
    // Level-0 cells of m_cell_size meters cover the content; scene leaf
    // overrides (level != 0) subdivide or merge cells. Tile boundaries
    // depend only on the grid parameters and the overrides - never on the
    // content - so they are stable across edits and sessions, and each
    // tile's nominal texel density is m_tile_size / its cell side.
    Grid_split result;
    const float base = std::max(m_cell_size, 0.01f);

    // 1. Occupied level-0 cells, AABB-conservative: a clipped fragment can
    //    land in any cell its source's bounds overlap. Degenerate bounds
    //    occupy their center cell. A pathological span (a sky sphere
    //    flagged lightmapped) is clamped with a warning instead of
    //    emitting millions of cells.
    std::unordered_set<Lightmap_tile_key, Lightmap_tile_key_hash> occupied;
    erhe::math::Aabb content_bounds{};
    constexpr int max_cells_per_region_axis = 256;
    for (const erhe::math::Aabb& bounds : region_bounds) {
        if (!bounds.is_valid()) {
            continue;
        }
        content_bounds.include(bounds);
        int ix0 = static_cast<int>(std::floor(bounds.min.x / base));
        int ix1 = static_cast<int>(std::floor(bounds.max.x / base));
        int iz0 = static_cast<int>(std::floor(bounds.min.z / base));
        int iz1 = static_cast<int>(std::floor(bounds.max.z / base));
        if (((ix1 - ix0) > max_cells_per_region_axis) || ((iz1 - iz0) > max_cells_per_region_axis)) {
            if (m_report != nullptr) {
                m_report->add_warning(
                    Lightmap_report::Stage::layout,
                    "grid split",
                    fmt::format(
                        "a region spans {}x{} cells of {} m - clamped to {} cells per axis around its center (raise cell_size_m for content this large)",
                        ix1 - ix0 + 1, iz1 - iz0 + 1, base, max_cells_per_region_axis
                    )
                );
            }
            const glm::vec3 center = bounds.center();
            const int cx = static_cast<int>(std::floor(center.x / base));
            const int cz = static_cast<int>(std::floor(center.z / base));
            ix0 = std::max(ix0, cx - max_cells_per_region_axis / 2);
            ix1 = std::min(ix1, cx + max_cells_per_region_axis / 2);
            iz0 = std::max(iz0, cz - max_cells_per_region_axis / 2);
            iz1 = std::min(iz1, cz + max_cells_per_region_axis / 2);
        }
        for (int iz = iz0; iz <= iz1; ++iz) {
            for (int ix = ix0; ix <= ix1; ++ix) {
                occupied.insert(Lightmap_tile_key{0, ix, iz});
            }
        }
    }
    if (occupied.empty()) {
        return result;
    }

    // 2. Apply overrides: stored non-default leaves replace the level-0
    //    cells they cover. Tolerant descent - a base cell with subdivide
    //    leaves below it recurses; quadrants without a stored leaf become
    //    implicit leaves at their own level, so partially stored data
    //    degrades gracefully instead of failing.
    std::unordered_set<Lightmap_tile_key, Lightmap_tile_key_hash> stored;
    std::unordered_set<Lightmap_tile_key, Lightmap_tile_key_hash> stored_interior; // strict ancestors of stored subdivide leaves
    int min_override_level = 0;
    for (const glm::ivec3& value : m_tile_overrides) {
        const Lightmap_tile_key key{value};
        if (key.level == 0) {
            continue; // level 0 is the default; never stored
        }
        stored.insert(key);
        min_override_level = std::min(min_override_level, key.level);
        if (key.level > 0) {
            Lightmap_tile_key ancestor = key.parent();
            while (ancestor.level >= 0) {
                stored_interior.insert(ancestor);
                if (ancestor.level == 0) {
                    break;
                }
                ancestor = ancestor.parent();
            }
        }
    }

    std::vector<Lightmap_tile_key> leaves;
    std::unordered_set<Lightmap_tile_key, Lightmap_tile_key_hash> emitted;
    const auto emit_leaf = [&leaves, &emitted](const Lightmap_tile_key& key) {
        if (emitted.insert(key).second) {
            leaves.push_back(key);
        }
    };
    const std::function<void(const Lightmap_tile_key&)> descend = [&](const Lightmap_tile_key& key) {
        if ((key.level > 0) && stored.contains(key)) {
            emit_leaf(key);
            return;
        }
        if (stored_interior.contains(key)) {
            for (const Lightmap_tile_key& child : key.children()) {
                descend(child);
            }
            return;
        }
        emit_leaf(key);
    };
    for (const Lightmap_tile_key& cell : occupied) {
        // Merged ancestor wins (coarsest stored ancestor).
        bool merged = false;
        for (int level = min_override_level; level < 0; ++level) {
            const int shift = 0 - level;
            const Lightmap_tile_key ancestor{level, cell.ix >> shift, cell.iz >> shift};
            if (stored.contains(ancestor)) {
                emit_leaf(ancestor);
                merged = true;
                break;
            }
        }
        if (!merged) {
            descend(cell);
        }
    }
    std::sort(leaves.begin(), leaves.end());

    // 3. Tiles, index-aligned with the kd leaf emission below.
    result.tiles.reserve(leaves.size());
    for (const Lightmap_tile_key& key : leaves) {
        Tile tile;
        tile.key              = key;
        tile.texels_per_meter = static_cast<float>(m_tile_size) / key.cell_size(base);
        const glm::vec2 lo = key.min_corner(base);
        const glm::vec2 hi = key.max_corner(base);
        tile.cell_bounds.include(glm::vec3{lo.x, content_bounds.min.y, lo.y});
        tile.cell_bounds.include(glm::vec3{hi.x, content_bounds.max.y, hi.y});
        result.tile_of_key.emplace(key, static_cast<int>(result.tiles.size()));
        result.tiles.push_back(std::move(tile));
    }

    // 4. kd tree over the leaf set, each quadtree split emitted as an X
    //    plane + two Z planes so clip_by_tile_tree cuts against the same
    //    partition unchanged. A world-origin-anchored quadtree has NO
    //    aligned cell spanning the origin (cells with ix >= 0 and ix < 0
    //    never share an aligned ancestor), so the tree starts with a fixed
    //    origin cross - x = 0, then z = 0 per side - and one aligned
    //    subtree per occupied signed quadrant. Quadrants with no leaf
    //    below become tile -1 leaves (occupancy is AABB-conservative, so
    //    no fragment routes there; the partitioner drops any that do).
    std::vector<erhe::geometry::operation::Clip_tree_node>& kd_nodes = result.kd_nodes;
    const auto ancestor_at = [](const Lightmap_tile_key& key, const int level) -> Lightmap_tile_key {
        const int shift = key.level - level;
        return Lightmap_tile_key{level, key.ix >> shift, key.iz >> shift};
    };
    const auto emit_empty_leaf = [&kd_nodes]() -> int {
        const int node_index = static_cast<int>(kd_nodes.size());
        kd_nodes.emplace_back();
        kd_nodes[static_cast<std::size_t>(node_index)].tile = -1;
        return node_index;
    };
    // One signed origin quadrant's leaves (all same XZ signs): find the
    // smallest aligned ancestor cell containing them all (converges within
    // a quadrant: same-sign indices shift toward 0 / -1), then emit the
    // quadtree below it.
    const auto emit_subtree = [&](const std::vector<Lightmap_tile_key>& subtree_leaves) -> int {
        if (subtree_leaves.empty()) {
            return emit_empty_leaf();
        }
        int root_level = 0;
        for (const Lightmap_tile_key& key : subtree_leaves) {
            root_level = std::min(root_level, key.level);
        }
        while (true) {
            const Lightmap_tile_key root = ancestor_at(subtree_leaves.front(), root_level);
            bool contains_all = true;
            for (const Lightmap_tile_key& key : subtree_leaves) {
                if (ancestor_at(key, root_level) != root) {
                    contains_all = false;
                    break;
                }
            }
            if (contains_all) {
                break;
            }
            --root_level;
        }
        std::unordered_set<Lightmap_tile_key, Lightmap_tile_key_hash> has_leaf_below;
        for (const Lightmap_tile_key& key : subtree_leaves) {
            for (int level = key.level - 1; level >= root_level; --level) {
                has_leaf_below.insert(ancestor_at(key, level));
            }
        }
        const std::function<int(const Lightmap_tile_key&)> emit_node = [&](const Lightmap_tile_key& key) -> int {
            const int node_index = static_cast<int>(kd_nodes.size());
            kd_nodes.emplace_back();
            const auto leaf_it = result.tile_of_key.find(key);
            if (leaf_it != result.tile_of_key.end()) {
                kd_nodes[static_cast<std::size_t>(node_index)].tile = leaf_it->second;
                return node_index;
            }
            if (!has_leaf_below.contains(key)) {
                kd_nodes[static_cast<std::size_t>(node_index)].tile = -1; // empty quadrant
                return node_index;
            }
            const float s      = key.cell_size(base);
            const float mid_x  = (static_cast<float>(key.ix) + 0.5f) * s;
            const float mid_z  = (static_cast<float>(key.iz) + 0.5f) * s;
            const std::array<Lightmap_tile_key, 4> quads = key.children(); // {2ix,2iz},{2ix+1,2iz},{2ix,2iz+1},{2ix+1,2iz+1}
            kd_nodes[static_cast<std::size_t>(node_index)].axis  = 0;
            kd_nodes[static_cast<std::size_t>(node_index)].value = mid_x;
            const auto emit_z_pair = [&](const Lightmap_tile_key& low_z, const Lightmap_tile_key& high_z) -> int {
                const int z_index = static_cast<int>(kd_nodes.size());
                kd_nodes.emplace_back();
                kd_nodes[static_cast<std::size_t>(z_index)].axis  = 2;
                kd_nodes[static_cast<std::size_t>(z_index)].value = mid_z;
                const int child_0 = emit_node(low_z);
                const int child_1 = emit_node(high_z);
                kd_nodes[static_cast<std::size_t>(z_index)].child[0] = child_0;
                kd_nodes[static_cast<std::size_t>(z_index)].child[1] = child_1;
                return z_index;
            };
            const int west = emit_z_pair(quads[0], quads[2]);
            const int east = emit_z_pair(quads[1], quads[3]);
            kd_nodes[static_cast<std::size_t>(node_index)].child[0] = west;
            kd_nodes[static_cast<std::size_t>(node_index)].child[1] = east;
            return node_index;
        };
        return emit_node(ancestor_at(subtree_leaves.front(), root_level));
    };

    // Partition the leaves by origin quadrant. A cell never straddles the
    // origin, so the sign of its indices is the sign of its extent.
    std::array<std::vector<Lightmap_tile_key>, 4> quadrant_leaves; // [west/south, east/south, west/north, east/north]
    for (const Lightmap_tile_key& key : leaves) {
        const std::size_t quadrant = ((key.ix >= 0) ? 1u : 0u) + ((key.iz >= 0) ? 2u : 0u);
        quadrant_leaves[quadrant].push_back(key);
    }
    int occupied_quadrants = 0;
    std::size_t single_quadrant = 0;
    for (std::size_t q = 0; q < 4; ++q) {
        if (!quadrant_leaves[q].empty()) {
            ++occupied_quadrants;
            single_quadrant = q;
        }
    }
    if (occupied_quadrants == 1) {
        emit_subtree(quadrant_leaves[single_quadrant]);
        return result;
    }
    // Origin cross: root splits X at 0; each side splits Z at 0.
    kd_nodes.emplace_back();
    kd_nodes[0].axis  = 0;
    kd_nodes[0].value = 0.0f;
    const auto emit_origin_z = [&](const std::vector<Lightmap_tile_key>& south, const std::vector<Lightmap_tile_key>& north) -> int {
        const int z_index = static_cast<int>(kd_nodes.size());
        kd_nodes.emplace_back();
        kd_nodes[static_cast<std::size_t>(z_index)].axis  = 2;
        kd_nodes[static_cast<std::size_t>(z_index)].value = 0.0f;
        const int child_0 = emit_subtree(south);
        const int child_1 = emit_subtree(north);
        kd_nodes[static_cast<std::size_t>(z_index)].child[0] = child_0;
        kd_nodes[static_cast<std::size_t>(z_index)].child[1] = child_1;
        return z_index;
    };
    const int west = emit_origin_z(quadrant_leaves[0], quadrant_leaves[2]);
    const int east = emit_origin_z(quadrant_leaves[1], quadrant_leaves[3]);
    kd_nodes[0].child[0] = west;
    kd_nodes[0].child[1] = east;
    return result;
}

void Lightmap_baker::pack_tile_regions(
    const int                       tile_index,
    Tile&                           tile,
    std::vector<Instance_region>&   regions,
    const std::vector<std::size_t>& members,
    const float                     min_face_texels,
    std::vector<Instance_region>&   out_packed_regions
)
{
    if (members.empty()) {
        return;
    }
    const int   tile_size       = m_tile_size;
    const int   max_region_side = tile_size - 2 * s_padding;
    const float tile_tpm        = tile.texels_per_meter;

    std::vector<std::size_t> order = members;
    const auto desired_side_of = [&regions, tile_tpm, min_face_texels](const std::size_t i) -> float {
        return desired_region_side(regions[i], tile_tpm, min_face_texels);
    };
    std::sort(order.begin(), order.end(), [&desired_side_of](const std::size_t l, const std::size_t r) {
        return desired_side_of(l) > desired_side_of(r);
    });

    // Down-only density flex: the tile's nominal density (texels_per_meter)
    // is the ceiling; content that does not fit shrinks uniformly. Below
    // the 1% floor the remaining regions are dropped with an error - the
    // fix is subdividing the tile (or a larger tile_texture_size).
    float density_scale = 1.0f;
    for (const std::size_t i : order) {
        const float side = desired_side_of(i);
        if (side > static_cast<float>(max_region_side)) {
            density_scale = std::min(density_scale, static_cast<float>(max_region_side) / side);
        }
    }
    for (;;) {
        rbp::SkylineBinPack packer;
        packer.Init(tile_size, tile_size, false);
        std::vector<Instance_region> tile_regions;
        tile_regions.reserve(order.size());
        std::size_t packed_count = 0;
        for (const std::size_t i : order) {
            const int side = std::clamp(static_cast<int>(std::ceil(desired_side_of(i) * density_scale)), 4, max_region_side);
            const rbp::Rect rect = packer.Insert(side + 2 * s_padding, side + 2 * s_padding, rbp::SkylineBinPack::LevelBottomLeft);
            if ((rect.width == 0) || (rect.height == 0)) {
                break;
            }
            Instance_region region = regions[i];
            region.x      = rect.x + s_padding;
            region.y      = rect.y + s_padding;
            region.width  = side;
            region.height = side;
            region.tile   = tile_index;
            const float inv_tile = 1.0f / static_cast<float>(tile_size);
            region.uv_scale_offset = glm::vec4{
                static_cast<float>(region.width)  * inv_tile,
                static_cast<float>(region.height) * inv_tile,
                static_cast<float>(region.x)      * inv_tile,
                static_cast<float>(region.y)      * inv_tile
            };
            tile_regions.push_back(std::move(region));
            ++packed_count;
        }
        const bool give_up = density_scale < 0.01f;
        if ((packed_count == order.size()) || give_up) {
            if (give_up && (packed_count < order.size()) && (m_report != nullptr)) {
                m_report->add_error(
                    Lightmap_report::Stage::layout,
                    "grid pack",
                    fmt::format(
                        "tile ({},{},{}): {} of {} regions do not fit even at minimum density - subdivide the tile or increase tile_texture_size",
                        tile.key.level, tile.key.ix, tile.key.iz, order.size() - packed_count, order.size()
                    )
                );
            }
            if ((density_scale < 0.999f) && (packed_count == order.size()) && (m_report != nullptr)) {
                m_report->add_warning(
                    Lightmap_report::Stage::layout,
                    "density flex",
                    fmt::format(
                        "tile ({},{},{}): texel density scaled to {:.0f}% of its nominal {:.0f} texels/m to fit {}^2 - subdivide the tile to restore density",
                        tile.key.level, tile.key.ix, tile.key.iz, 100.0f * density_scale, tile_tpm, tile_size
                    )
                );
            }
            tile.density_scale = density_scale;
            tile.has_content   = tile.has_content || (packed_count > 0);
            for (Instance_region& region : tile_regions) {
                out_packed_regions.push_back(std::move(region));
            }
            return;
        }
        density_scale *= 0.95f;
    }
}

auto Lightmap_baker::compute_tile_split_estimate(Scene_root& scene_root) -> Estimate_split
{
    Estimate_split result;

    // Same enumeration as update_layout minus the channel-2 UV requirement:
    // the split needs only geometry (world areas + bounding boxes), so the
    // partitioner can run before any unwrap exists.
    std::size_t seen_meshes      = 0;
    std::size_t skip_not_flagged = 0;
    std::size_t skip_no_shape    = 0;

    std::vector<Instance_region> regions;
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || mesh->skin) {
            continue;
        }
        ++seen_meshes;
        if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
            ++skip_not_flagged;
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
                ++skip_no_shape;
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
            if (!geometry) {
                ++skip_no_shape;
                continue;
            }
            Instance_region region;
            region.mesh            = mesh;
            region.primitive_index = primitive_index;
            region.world_area      = mesh_surface_area(geometry->get_mesh()) * instance_area_scale;
            // Estimated coverage instead of compute_region_uv_metrics; the
            // min-face bound stays off (min_face_texels 0 below) because
            // min_facet_uv_extent cannot be measured pre-unwrap - the
            // partitioned relayout applies the real bound per tile.
            region.uv_coverage     = c_estimated_uv_coverage;
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty()) {
        const std::string message = fmt::format(
            "no lightmapped meshes to partition (content meshes {}, skipped: not lightmapped {}, no render shape/geometry {})",
            seen_meshes,
            skip_not_flagged,
            skip_no_shape
        );
        log_render->info("Lightmap_baker::compute_tile_split_estimate: {}", message);
        if ((m_report != nullptr) && (seen_meshes > 0) && (skip_not_flagged < seen_meshes)) {
            m_report->add_warning(Lightmap_report::Stage::layout, "split estimate", message);
        }
        return result;
    }

    // The grid is content-independent (cells + overrides only), so the
    // "estimate" is exact: the tree the pieces are clipped against is the
    // same tree every relayout reproduces from the same grid parameters.
    std::vector<erhe::math::Aabb> bounds;
    bounds.reserve(regions.size());
    for (const Instance_region& region : regions) {
        bounds.push_back(region_world_bounds(region));
    }
    Grid_split grid = build_grid_split(bounds);
    if (grid.tiles.empty()) {
        if (m_report != nullptr) {
            m_report->add_warning(Lightmap_report::Stage::layout, "split estimate", "grid split produced no tiles");
        }
        return result;
    }
    result.regions.reserve(regions.size());
    for (std::size_t i = 0; i < regions.size(); ++i) {
        const glm::vec3 center = bounds[i].is_valid() ? bounds[i].center() : glm::vec3{0.0f};
        const int tile = grid.tile_for_position(glm::vec2{center.x, center.z}, std::max(m_cell_size, 0.01f));
        result.regions.push_back(Estimate_region{std::move(regions[i].mesh), regions[i].primitive_index, tile});
    }
    result.kd_nodes   = std::move(grid.kd_nodes);
    result.tiles      = std::move(grid.tiles);
    result.tile_count = static_cast<int>(result.tiles.size());
    return result;
}

auto Lightmap_baker::finalize_layout(
    std::vector<Tile>&&                                      tiles,
    std::vector<Instance_region>&&                           packed_regions,
    std::vector<erhe::geometry::operation::Clip_tree_node>&& kd_nodes,
    const bool                                               partitioned
) -> bool
{
    const int tile_size = m_tile_size;

    // ---- Display slot grid: ceil(sqrt(resident budget)) tile-sized slots
    // per axis, shrunk while the persistent targets (per-resident-tile fp32
    // accumulation + the fp16 display atlas) exceed the device memory
    // budget - streaming worlds degrade to fewer resident tiles instead of
    // failing. ----
    int slot_grid = 1;
    {
        const int desired_slots = std::clamp(m_slot_budget, 1, static_cast<int>(tiles.size()));
        while (slot_grid * slot_grid < desired_slots) {
            ++slot_grid;
        }
        const erhe::graphics::Memory_budget budget = m_graphics_device.get_memory_budget();
        if (budget.is_known()) {
            const uint64_t tile_texels     = static_cast<uint64_t>(tile_size) * static_cast<uint64_t>(tile_size);
            const uint64_t bytes_per_texel = persistent_bytes_per_texel();
            uint64_t available = budget.get_remaining();
            if (m_display_texture) {
                // The current display atlas is about to be replaced; its
                // bytes count as available.
                available += static_cast<uint64_t>(erhe::dataformat::get_format_size_bytes(c_atlas_format))
                    * static_cast<uint64_t>(m_display_texture->get_width())
                    * static_cast<uint64_t>(m_display_texture->get_height());
            }
            // Reserve the tile-sized scratch working set (G-buffer x4,
            // working atlas, dilate scratch) off the top; it does not scale
            // with the slot count.
            const uint64_t scratch_bytes = scratch_bytes_per_texel() * tile_texels;
            available = (available > scratch_bytes) ? (available - scratch_bytes) : 0;
            const uint64_t usable = (available * c_budget_numerator) / c_budget_denominator;
            while (
                (slot_grid > 1) &&
                (bytes_per_texel * tile_texels * static_cast<uint64_t>(slot_grid) * static_cast<uint64_t>(slot_grid) > usable)
            ) {
                --slot_grid;
            }
            if (slot_grid * slot_grid < desired_slots) {
                const std::string message = fmt::format(
                    "resident tiles capped at {} (wanted {}) by device memory budget ({} MB remaining of {} MB)",
                    slot_grid * slot_grid, desired_slots,
                    budget.get_remaining() / (1024u * 1024u),
                    budget.device_local_budget / (1024u * 1024u)
                );
                log_render->info("Lightmap_baker::update_layout: {}", message);
                if (m_report != nullptr) {
                    m_report->add_warning(Lightmap_report::Stage::layout, "memory budget", message);
                }
            }
        }
    }

    m_layout.width     = slot_grid * tile_size;
    m_layout.height    = slot_grid * tile_size;
    m_layout.slots_x   = slot_grid;
    m_layout.slots_y   = slot_grid;
    m_layout.tile_size   = tile_size;
    m_layout.tiles       = std::move(tiles);
    m_layout.regions     = std::move(packed_regions);
    m_layout.kd_nodes    = std::move(kd_nodes);
    m_layout.partitioned = partitioned;

    // Initial residency: the first slots' worth of CONTENT tiles in index
    // order (grid tiles without packed regions never bake or hold a slot);
    // the interactive tick re-ranks by camera distance every frame.
    const int slot_count = m_layout.get_slot_count();
    {
        int next_slot = 0;
        for (int tile = 0; tile < m_layout.get_tile_count(); ++tile) {
            Tile& layout_tile = m_layout.tiles[static_cast<std::size_t>(tile)];
            layout_tile.slot = (layout_tile.has_content && (next_slot < slot_count)) ? next_slot++ : -1;
        }
    }

    m_tiles.assign(m_layout.tiles.size(), Tile_state{});
    for (std::size_t i = 0; i < m_tiles.size(); ++i) {
        m_tiles[i].has_content = m_layout.tiles[i].has_content;
        m_tiles[i].active      = m_layout.tiles[i].slot >= 0;
    }
    {
        const std::chrono::steady_clock::time_point seam_start = std::chrono::steady_clock::now();
        build_seam_vertices();
        const std::chrono::steady_clock::time_point seam_end = std::chrono::steady_clock::now();
        log_render->info(
            "Lightmap_baker::finalize_layout timing: build_seam_vertices {:.1f} ms",
            static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(seam_end - seam_start).count()) / 1000.0
        );
    }
    log_render->info(
        "Lightmap_baker::update_layout: {} regions in {} spatial tiles of {}^2 texels, {} display slots ({}x{} atlas){}",
        m_layout.regions.size(), m_layout.get_tile_count(), tile_size,
        m_layout.get_slot_count(), m_layout.width, m_layout.height,
        m_layout.partitioned ? " [partitioned world-space pieces]" : ""
    );
    return true;
}

auto Lightmap_baker::update_layout_partitioned(Scene_root& scene_root, const float min_face_texels) -> bool
{
    static_cast<void>(scene_root);
    const Lightmap_partitioner& partitioner = *m_partitioner;
    const int                   tile_count  = partitioner.get_tile_count();

    // Tiles come from the partition's grid split (grid keys, cell bounds,
    // per-tile nominal density) - never re-derived from the live pieces.
    std::vector<Tile> tiles = partitioner.get_tile_descs();
    if (static_cast<int>(tiles.size()) != tile_count) {
        if (m_report != nullptr) {
            m_report->add_error(Lightmap_report::Stage::layout, "partitioned layout", "partition tile metadata is inconsistent - re-prepare");
        }
        m_seam_vertices.clear();
        return false;
    }
    for (Tile& tile : tiles) {
        tile.world_bounds  = {};
        tile.density_scale = 1.0f;
        tile.has_content   = false;
        tile.slot          = -1;
    }

    const std::chrono::steady_clock::time_point metrics_start = std::chrono::steady_clock::now();

    // One region per piece Mesh_primitive; the tile assignment comes from
    // the partition (each piece was clipped to exactly one tile), never
    // from a re-split of the live pieces.
    std::vector<Instance_region> regions;
    for (const Lightmap_partitioner::Original_entry& entry : partitioner.get_entries()) {
        if (!entry.piece_mesh) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = entry.piece_mesh->get_primitives();
        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const erhe::primitive::Primitive* const primitive = primitives[primitive_index].primitive.get();
            if ((primitive == nullptr) || !primitive->render_shape) {
                continue;
            }
            const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
            if (!geometry || !geometry->get_attributes().corner_texcoord_2.has(0)) {
                continue;
            }
            if (primitive_index >= entry.pieces.size()) {
                continue;
            }
            const Lightmap_partitioner::Piece_info& piece = entry.pieces[primitive_index];
            if ((piece.tile < 0) || (piece.tile >= tile_count)) {
                continue;
            }
            Instance_region region;
            region.mesh                   = entry.piece_mesh;
            region.primitive_index        = primitive_index;
            // Pieces are world-space geometry on identity nodes: the local
            // surface area IS the world area.
            region.world_area             = mesh_surface_area(geometry->get_mesh());
            region.tile                   = piece.tile;
            region.source_primitive_index = piece.source_primitive_index;
            region.piece_ordinal          = piece.ordinal;
            compute_region_uv_metrics(*geometry, region);
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty() || (tile_count <= 0)) {
        if (m_report != nullptr) {
            m_report->add_error(Lightmap_report::Stage::layout, "partitioned layout", "no piece regions to pack");
        }
        m_seam_vertices.clear();
        return false;
    }

    std::vector<std::vector<std::size_t>> buckets(static_cast<std::size_t>(tile_count));
    for (std::size_t i = 0; i < regions.size(); ++i) {
        buckets[static_cast<std::size_t>(regions[i].tile)].push_back(i);
        const std::vector<erhe::scene::Mesh_primitive>& primitives = regions[i].mesh->get_primitives();
        const erhe::primitive::Primitive* const primitive = primitives[regions[i].primitive_index].primitive.get();
        if (primitive != nullptr) {
            // Pieces live on identity nodes: local bounds are world bounds.
            tiles[static_cast<std::size_t>(regions[i].tile)].world_bounds.include(primitive->get_bounding_box());
        }
    }

    const std::chrono::steady_clock::time_point pack_start = std::chrono::steady_clock::now();

    // Pack each tile's pieces (skyline, big-first) at the tile's nominal
    // density (tile_texture_size / cell side); the tiles are fixed, so a
    // set that does not fit can only flex its density down.
    std::vector<Instance_region> packed_regions;
    packed_regions.reserve(regions.size());
    for (int tile = 0; tile < tile_count; ++tile) {
        pack_tile_regions(tile, tiles[static_cast<std::size_t>(tile)], regions, buckets[static_cast<std::size_t>(tile)], min_face_texels, packed_regions);
    }
    {
        const std::chrono::steady_clock::time_point pack_end = std::chrono::steady_clock::now();
        const auto span_ms = [](const std::chrono::steady_clock::time_point from, const std::chrono::steady_clock::time_point to) {
            return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(to - from).count()) / 1000.0;
        };
        log_render->info(
            "Lightmap_baker::update_layout_partitioned timing: region metrics {:.1f} ms ({} pieces), packing {:.1f} ms ({} tiles)",
            span_ms(metrics_start, pack_start), regions.size(),
            span_ms(pack_start, pack_end), tile_count
        );
    }
    if (packed_regions.empty()) {
        if (m_report != nullptr) {
            m_report->add_error(Lightmap_report::Stage::layout, "partitioned layout", "no piece regions could be packed");
        }
        m_seam_vertices.clear();
        return false;
    }

    std::vector<erhe::geometry::operation::Clip_tree_node> kd_nodes = partitioner.get_tile_tree();
    return finalize_layout(std::move(tiles), std::move(packed_regions), std::move(kd_nodes), true);
}

void Lightmap_baker::build_seam_vertices()
{
    m_seam_vertices.clear();
    m_tile_seam_ranges.assign(static_cast<std::size_t>(m_layout.get_tile_count()), {0u, 0u});
    std::size_t seam_count = 0;
    // Grouped by tile (the seam pass rasters into the tile-sized working
    // atlas, one tile per publish): both sides of a seam always live in the
    // same region, hence the same tile.
    for (int tile = 0; tile < m_layout.get_tile_count(); ++tile) {
    const uint32_t  tile_first_vertex = static_cast<uint32_t>(m_seam_vertices.size());
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh || (region.tile != tile)) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
        if (region.primitive_index >= primitives.size()) {
            continue;
        }
        const erhe::primitive::Primitive* const primitive = primitives[region.primitive_index].primitive.get();
        if ((primitive == nullptr) || !primitive->render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
        if (!geometry) {
            continue;
        }
        erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
        if (!attributes.corner_texcoord_2.has(0)) {
            continue;
        }
        const GEO::Mesh& geo_mesh = geometry->get_mesh();
        const glm::vec2 uv_scale {region.uv_scale_offset.x, region.uv_scale_offset.y};
        const glm::vec2 uv_offset{region.uv_scale_offset.z, region.uv_scale_offset.w};
        // The seam pass rasters into the TILE-sized working atlas and the
        // region's uv_scale_offset is already tile-local.
        const auto atlas_uv = [&](const GEO::vec2f& uv) -> glm::vec2 {
            return glm::vec2{uv.x, uv.y} * uv_scale + uv_offset;
        };

        // First occurrence of each facet edge, keyed by the (order-
        // normalized) vertex id pair; the second occurrence with different
        // UVs is a seam. Shared vertex ids make the position test exact.
        class Edge_side
        {
        public:
            GEO::vec2f uv0;
            GEO::vec2f uv1;
            GEO::vec3f n0;
            GEO::vec3f n1;
            bool       seam_emitted{false};
        };
        std::unordered_map<uint64_t, Edge_side> edge_map;
        edge_map.reserve(geo_mesh.facet_corners.nb());

        for (GEO::index_t facet : geo_mesh.facets) {
            const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
            for (GEO::index_t k = 0; k < corner_count; ++k) {
                const GEO::index_t c0 = geo_mesh.facets.corner(facet, k);
                const GEO::index_t c1 = geo_mesh.facets.corner(facet, (k + 1) % corner_count);
                GEO::index_t v0 = geo_mesh.facet_corners.vertex(c0);
                GEO::index_t v1 = geo_mesh.facet_corners.vertex(c1);
                if (v0 == v1) {
                    continue;
                }
                const std::optional<GEO::vec2f> uv0_opt = attributes.corner_texcoord_2.try_get(c0);
                const std::optional<GEO::vec2f> uv1_opt = attributes.corner_texcoord_2.try_get(c1);
                if (!uv0_opt.has_value() || !uv1_opt.has_value()) {
                    continue;
                }
                GEO::vec2f uv0 = uv0_opt.value();
                GEO::vec2f uv1 = uv1_opt.value();
                GEO::vec3f n0  = attributes.corner_normal.try_get(c0).value_or(GEO::vec3f{0.0f, 0.0f, 0.0f});
                GEO::vec3f n1  = attributes.corner_normal.try_get(c1).value_or(GEO::vec3f{0.0f, 0.0f, 0.0f});
                if (v1 < v0) {
                    std::swap(v0, v1);
                    std::swap(uv0, uv1);
                    std::swap(n0, n1);
                }
                const uint64_t key = (static_cast<uint64_t>(v0) << 32) | static_cast<uint64_t>(v1);
                const auto found = edge_map.find(key);
                if (found == edge_map.end()) {
                    edge_map.emplace(key, Edge_side{uv0, uv1, n0, n1, false});
                    continue;
                }
                Edge_side& other = found->second;
                const float uv_epsilon = 1.0e-6f;
                if ((GEO::length2(other.uv0 - uv0) < uv_epsilon) && (GEO::length2(other.uv1 - uv1) < uv_epsilon)) {
                    continue; // same UV space, not a seam
                }
                if (other.seam_emitted) {
                    continue; // bad geometry (edge shared by 3+ facets)
                }
                // Hard edges (split corner normals) have genuinely
                // discontinuous lighting - do not blend across them. Zero
                // normals (attribute absent) blend permissively.
                const float l0 = GEO::length(n0) * GEO::length(other.n0);
                const float l1 = GEO::length(n1) * GEO::length(other.n1);
                if (((l0 > 0.0f) && (GEO::dot(n0, other.n0) < 0.99f * l0)) ||
                    ((l1 > 0.0f) && (GEO::dot(n1, other.n1) < 0.99f * l1))) {
                    continue;
                }
                other.seam_emitted = true;
                ++seam_count;
                // Two lines: each side rasterized at its own UVs, sampling
                // the opposite side.
                m_seam_vertices.push_back(Seam_vertex{atlas_uv(uv0),       atlas_uv(other.uv0)});
                m_seam_vertices.push_back(Seam_vertex{atlas_uv(uv1),       atlas_uv(other.uv1)});
                m_seam_vertices.push_back(Seam_vertex{atlas_uv(other.uv0), atlas_uv(uv0)});
                m_seam_vertices.push_back(Seam_vertex{atlas_uv(other.uv1), atlas_uv(uv1)});
            }
        }
    }
    m_tile_seam_ranges[static_cast<std::size_t>(tile)] = {
        tile_first_vertex,
        static_cast<uint32_t>(m_seam_vertices.size()) - tile_first_vertex
    };
    } // for tile
    log_render->info("Lightmap_baker: {} seam edges ({} line vertices)", seam_count, m_seam_vertices.size());
}

void Lightmap_baker::ensure_gbuffer_targets()
{
    using namespace erhe::graphics;
    const auto make_target = [this](const char* label, erhe::dataformat::Format format, const int width, const int height) {
        return std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                // storage: the virtual-offset adjust pass rewrites the
                // position G-buffer in place (reads normal the same way).
                .usage_mask  =
                    Image_usage_flag_bit_mask::color_attachment |
                    Image_usage_flag_bit_mask::sampled          |
                    Image_usage_flag_bit_mask::storage          |
                    Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    };
    // Every scratch target is CELL-sized: the G-buffer holds one tile tile
    // at a time (m_gbuffer_tile) and is re-rastered when the gather cursor
    // moves to another tile.
    const int tile_size = m_layout.get_tile_size();
    // Supersampled ray-origin target (Bake_options::supersample_factor):
    // the tile at factor x per axis, transient like the other G-buffer
    // targets. Released when the option is off; skipped (with a log in
    // bake_gbuffer) when the hi-res tile would exceed the dimension guard
    // or the factor's gather variant failed to build.
    const int factor = m_options.supersample_factor;
    const bool factor_has_gather =
        ((factor == 4) && m_gather_ss4_pipeline) ||
        ((factor == 8) && m_gather_ss8_pipeline);
    // Byte-budget guard on top of the dimension guard: the hi-res origin
    // target is factor^2 tiles of RGBA32F - the single largest scratch
    // allocation - so refuse it (feature off, bake still runs) when it
    // would eat more than half the remaining device-local budget.
    bool origin_fits_budget = true;
    if (factor_has_gather) {
        const uint64_t origin_bytes =
            erhe::dataformat::get_format_size_bytes(c_position_format)
            * static_cast<uint64_t>(tile_size) * static_cast<uint64_t>(factor)
            * static_cast<uint64_t>(tile_size) * static_cast<uint64_t>(factor);
        const erhe::graphics::Memory_budget budget = m_graphics_device.get_memory_budget();
        if (budget.is_known()) {
            uint64_t available = budget.get_remaining();
            if (m_origin_texture) {
                // A mismatched existing target is released before the new
                // one is created; its bytes are available to the check.
                available += erhe::dataformat::get_format_size_bytes(c_position_format)
                    * static_cast<uint64_t>(m_origin_texture->get_width())
                    * static_cast<uint64_t>(m_origin_texture->get_height());
            }
            origin_fits_budget = origin_bytes <= available / 2;
        }
    }
    const bool want_origin =
        factor_has_gather &&
        m_origin_pipeline &&
        origin_fits_budget &&
        (tile_size * factor <= c_supersample_max_dim);
    if (!want_origin) {
        if (m_origin_texture) {
            m_origin_texture.reset();
            m_origin_valid = false;
        }
    } else {
        const bool origin_matches =
            m_origin_texture &&
            (m_origin_texture->get_width()  == tile_size * factor) &&
            (m_origin_texture->get_height() == tile_size * factor);
        if (!origin_matches) {
            m_origin_texture = make_target(
                "lightmap ray origins",
                c_position_format,
                tile_size * factor,
                tile_size * factor
            );
            m_origin_valid  = false;
            m_gbuffer_valid = false;
        }
    }
    const bool matches =
        m_position_texture &&
        (m_position_texture->get_width()  == tile_size) &&
        (m_position_texture->get_height() == tile_size);
    if (matches) {
        // The smooth-position target is released once the one-shot adjust
        // pass folds it into the position G-buffer (record_adjust); a
        // re-raster needs it back.
        if (!m_smooth_position_texture) {
            m_smooth_position_texture = make_target("lightmap gbuffer smooth position", c_position_format, tile_size, tile_size);
        }
        return;
    }
    m_position_texture        = make_target("lightmap gbuffer position",        c_position_format, tile_size, tile_size);
    m_normal_texture          = make_target("lightmap gbuffer normal",          c_normal_format,   tile_size, tile_size);
    m_albedo_texture          = make_target("lightmap gbuffer albedo",          c_albedo_format,   tile_size, tile_size);
    m_smooth_position_texture = make_target("lightmap gbuffer smooth position", c_position_format, tile_size, tile_size);
    m_gbuffer_valid    = false;
    m_gbuffer_tile     = -1;
}

auto Lightmap_baker::bake_gbuffer(const int tile) -> bool
{
    using namespace erhe::graphics;

    if (!m_pipeline || (m_layout.width == 0) || m_layout.regions.empty()) {
        return false;
    }
    if ((tile < 0) || (tile >= m_layout.get_tile_count())) {
        return false;
    }

    const std::size_t not_skinned_key = m_mesh_memory.get_vertex_input_from_vertex_format(m_mesh_memory.vertex_format_not_skinned).key;

    ensure_gbuffer_targets();

    const int tile_size = m_layout.get_tile_size();

    // Texel coverage strategy (Bake_options::coverage_mode). Conservative:
    // native conservative rasterization, one unjittered pass (falls back to
    // 9-tap when the extension is missing). Jitter modes: multi-jitter
    // re-render (Bakery-style) - each region rasterizes once per tap with
    // sub-texel NDC offsets spanning +-half a texel (3x3 or 5x5 grid),
    // center tap LAST. Depth test is off, so later draws win: edge texels
    // whose center just misses every triangle still get a jittered write,
    // and properly covered texels end with the unjittered value.
    const bool conservative_requested = m_options.coverage_mode == Coverage_mode::conservative;
    const bool use_conservative       = conservative_requested && m_pipeline_conservative;
    std::vector<glm::vec2> jitter_taps;
    if (use_conservative) {
        jitter_taps.push_back(glm::vec2{0.0f, 0.0f});
    } else {
        const int grid_half = (m_options.coverage_mode == Coverage_mode::jitter_25) ? 2 : 1;
        for (int y = -grid_half; y <= grid_half; ++y) {
            for (int x = -grid_half; x <= grid_half; ++x) {
                if ((x == 0) && (y == 0)) {
                    continue;
                }
                jitter_taps.push_back(
                    glm::vec2{
                        static_cast<float>(x) / static_cast<float>(grid_half),
                        static_cast<float>(y) / static_cast<float>(grid_half)
                    }
                );
            }
        }
        jitter_taps.push_back(glm::vec2{0.0f, 0.0f}); // center last
    }
    const std::size_t jitter_count = jitter_taps.size();

    // Per-draw UBO: one record per region per jitter pass.
    const std::size_t ubo_bytes = m_layout.regions.size() * jitter_count * c_draw_ubo_stride;
    Buffer draw_ubo{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = ubo_bytes,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::uniform,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap gbuffer draw ubo"}
        }
    };
    {
        // Half-texel jitter magnitude in NDC ([-1,1] spans the tile).
        const float jitter_step = 0.5f * 2.0f / static_cast<float>(tile_size);
        const std::span<std::byte> mapped = draw_ubo.map_bytes(0, ubo_bytes);
        std::memset(mapped.data(), 0, ubo_bytes);
        for (std::size_t j = 0; j < jitter_count; ++j) {
            const glm::vec4 jitter_ndc{jitter_taps[j].x * jitter_step, jitter_taps[j].y * jitter_step, 0.0f, 0.0f};
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
                if (region.tile != tile) {
                    continue; // draw_regions skips these; the record stays zero
                }
                const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
                const glm::mat4 world_from_node = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
                glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
                if (region.mesh) {
                    const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
                    if ((region.primitive_index < primitives.size()) && primitives[region.primitive_index].material) {
                        const erhe::primitive::Material& material = *primitives[region.primitive_index].material;
                        // Diffuse albedo: metals have no diffuse lobe, so
                        // they bounce (and later receive) almost nothing.
                        base_color = glm::vec4{material.data.base_color * (1.0f - material.data.metallic), 1.0f};
                    }
                }
                // Region uv_scale_offset is already tile-local: exactly the
                // raster target space.
                const glm::vec4 tile_uv_scale_offset = region.uv_scale_offset;
                std::byte* const record = mapped.data() + (static_cast<std::size_t>(j) * m_layout.regions.size() + i) * c_draw_ubo_stride;
                std::memcpy(record + m_draw_block_world_offset,      &world_from_node,      sizeof(glm::mat4));
                std::memcpy(record + m_draw_block_uv_offset,         &tile_uv_scale_offset, sizeof(glm::vec4));
                std::memcpy(record + m_draw_block_jitter_offset,     &jitter_ndc,           sizeof(glm::vec4));
                std::memcpy(record + m_draw_block_base_color_offset, &base_color,           sizeof(glm::vec4));
            }
        }
        draw_ubo.unmap();
    }

    // Standalone submit on a dedicated thread slot (7 is the texture graph
    // export; 6 keeps this bake from disturbing either that or the open
    // frame command buffer on slot 0).
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();

    // Fresh targets start UNDEFINED; normalize so the render pass can use a
    // uniform layout_before.
    command_buffer.transition_texture_layout(*m_position_texture,        Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_normal_texture,          Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_albedo_texture,          Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_smooth_position_texture, Image_layout::shader_read_only_optimal);

    Texture* const gbuffer_targets[4] = { m_position_texture.get(), m_normal_texture.get(), m_albedo_texture.get(), m_smooth_position_texture.get() };
    Render_pass_descriptor descriptor{};
    for (int i = 0; i < 4; ++i) {
        Render_pass_attachment_descriptor& attachment = descriptor.color_attachments[i];
        attachment.texture       = gbuffer_targets[i];
        attachment.clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
        attachment.load_action   = Load_action::Clear;
        attachment.store_action  = Store_action::Store;
        attachment.usage_before  = Image_usage_flag_bit_mask::sampled;
        attachment.layout_before = Image_layout::shader_read_only_optimal;
        attachment.usage_after   = Image_usage_flag_bit_mask::sampled;
        attachment.layout_after  = Image_layout::shader_read_only_optimal;
    }
    descriptor.render_target_width  = tile_size;
    descriptor.render_target_height = tile_size;
    descriptor.debug_label = erhe::utility::Debug_label{"lightmap gbuffer"};

    // One jitter pass worth of region draws; shared by the G-buffer passes
    // and the supersample-origin pass (which draws the zero-jitter center
    // records once at hi-res).
    const auto draw_regions = [&](Render_command_encoder& encoder, Buffer& ubo, const std::size_t pass_index) -> std::size_t {
        std::size_t pass_drawn = 0;
        for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
            const Instance_region& region = m_layout.regions[i];
            if (!region.mesh || (region.tile != tile)) {
                continue;
            }
            const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
            if (region.primitive_index >= primitives.size()) {
                continue;
            }
            const erhe::primitive::Primitive* const primitive = primitives[region.primitive_index].primitive.get();
            if (primitive == nullptr) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* const buffer_mesh = primitive->get_renderable_mesh();
            if ((buffer_mesh == nullptr) || (buffer_mesh->triangle_fill_indices.index_count == 0)) {
                continue;
            }
            if (buffer_mesh->vertex_input_key != not_skinned_key) {
                // Pipeline vertex input is the non-skinned content format;
                // anything else (should not happen after the skin filter)
                // is skipped rather than mis-bound.
                continue;
            }
            for (std::size_t stream = 0; stream < buffer_mesh->vertex_buffer_ranges.size(); ++stream) {
                const erhe::primitive::Buffer_range& range = buffer_mesh->vertex_buffer_ranges[stream];
                encoder.set_vertex_buffer(m_mesh_memory.get_vertex_buffer(range), range.byte_offset, stream);
            }
            const erhe::primitive::Buffer_range& index_range = buffer_mesh->index_buffer_range;
            encoder.set_index_buffer(m_mesh_memory.get_index_buffer(index_range));
            const std::size_t record_index = pass_index * m_layout.regions.size() + i;
            encoder.set_buffer(Buffer_target::uniform, &ubo, record_index * c_draw_ubo_stride, m_draw_block_size, 0);

            const erhe::dataformat::Format index_format = m_mesh_memory.get_index_format(
                erhe::scene_renderer::Pool_buffer_identity{index_range.pool_id, index_range.buffer_id}
            );
            const std::uintptr_t index_offset =
                index_range.byte_offset +
                buffer_mesh->triangle_fill_indices.first_index * index_range.element_size;
            encoder.draw_indexed_primitives(
                Primitive_type::triangle,
                buffer_mesh->triangle_fill_indices.index_count,
                index_format,
                index_offset
            );
            ++pass_drawn;
        }
        return pass_drawn;
    };

    std::size_t drawn = 0;
    {
        Render_pass            render_pass{m_graphics_device, descriptor};
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        const Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, tile_size, tile_size);
        encoder.set_scissor_rect (0, 0, tile_size, tile_size);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_render_pipeline(use_conservative ? *m_pipeline_conservative : *m_pipeline);

        for (std::size_t j = 0; j < jitter_count; ++j) {
            const std::size_t pass_drawn = draw_regions(encoder, draw_ubo, j);
            if (j == 0) {
                drawn = pass_drawn;
            }
        }
    }

    // Supersample-origin pass (Frostbite Flux texel supersampling): the
    // regular sub-texel sample grid, rasterized as one hi-res center pass
    // (no conservative raster, no jitter - the valid points must lie ON
    // triangles). ensure_gbuffer_targets() created the target only when the
    // option is on, the pipelines exist and the hi-res page fits the guard.
    m_origin_valid  = false;
    m_origin_factor = 0;
    if ((m_options.supersample_factor > 0) && !m_origin_texture) {
        log_render->warn(
            "Lightmap_baker: supersampled ray origins unavailable ({}x{} tile x{} exceeds {}, exceeds the device memory budget, or pipelines missing)",
            tile_size, tile_size, m_options.supersample_factor, c_supersample_max_dim
        );
    }
    if (m_origin_texture) {
        Render_pipeline* const origin_pipeline =
            m_options.terminator_fix ? m_origin_pipeline.get() : m_origin_no_smooth_pipeline.get();
        if (origin_pipeline != nullptr) {
            const int ss_width  = m_origin_texture->get_width();
            const int ss_height = m_origin_texture->get_height();
            command_buffer.transition_texture_layout(*m_origin_texture, Image_layout::shader_read_only_optimal);
            Render_pass_descriptor origin_descriptor{};
            Render_pass_attachment_descriptor& origin_attachment = origin_descriptor.color_attachments[0];
            origin_attachment.texture       = m_origin_texture.get();
            origin_attachment.clear_value   = std::array<double, 4>{0.0, 0.0, 0.0, 0.0};
            origin_attachment.load_action   = Load_action::Clear;
            origin_attachment.store_action  = Store_action::Store;
            origin_attachment.usage_before  = Image_usage_flag_bit_mask::sampled;
            origin_attachment.layout_before = Image_layout::shader_read_only_optimal;
            origin_attachment.usage_after   = Image_usage_flag_bit_mask::sampled;
            origin_attachment.layout_after  = Image_layout::shader_read_only_optimal;
            origin_descriptor.render_target_width  = ss_width;
            origin_descriptor.render_target_height = ss_height;
            origin_descriptor.debug_label = erhe::utility::Debug_label{"lightmap ray origins"};
            {
                Render_pass            origin_pass{m_graphics_device, origin_descriptor};
                Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
                const Scoped_render_pass scoped{origin_pass, command_buffer};
                encoder.set_viewport_rect(0, 0, ss_width, ss_height);
                encoder.set_scissor_rect (0, 0, ss_width, ss_height);
                encoder.set_bind_group_layout(m_bind_group_layout.get());
                encoder.set_render_pipeline(*origin_pipeline);
                // Center (zero-jitter) records - always the LAST pass slot.
                draw_regions(encoder, draw_ubo, jitter_count - 1);
            }
            m_origin_valid  = true;
            m_origin_factor = m_options.supersample_factor;
        }
    }
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_gbuffer_valid    = drawn > 0;
    m_gbuffer_tile     = m_gbuffer_valid ? tile : -1;
    m_gbuffer_adjusted = false; // fresh positions need the virtual-offset pass
    const char* const coverage_name =
        use_conservative     ? "native conservative raster" :
        (jitter_count == 25) ? "25-tap jitter"              :
        conservative_requested ? "9-tap jitter (conservative raster unavailable)" : "9-tap jitter";
    log_render->info(
        "Lightmap_baker: G-buffer baked, tile {}/{}, {} of {} regions drawn, {}x{}, {}{}",
        tile, m_layout.get_tile_count(),
        drawn, m_layout.regions.size(), tile_size, tile_size,
        coverage_name,
        m_origin_valid ? fmt::format(", supersampled origins {}x{} per texel", m_origin_factor, m_origin_factor) : ""
    );
    return m_gbuffer_valid;
}

auto Lightmap_baker::debug_write_gbuffer_pngs(const std::string& base_path) -> bool
{
    using namespace erhe::graphics;
    if (!m_gbuffer_valid || !m_position_texture || !m_normal_texture) {
        return false;
    }
    const int         width       = m_position_texture->get_width();
    const int         height      = m_position_texture->get_height();
    const std::size_t texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<float> position_data;
    std::vector<float> normal_data;
    std::vector<float> smooth_data;
    if (
        !read_rgba_texture_to_float(m_graphics_device, *m_position_texture, position_data) ||
        !read_rgba_texture_to_float(m_graphics_device, *m_normal_texture,   normal_data)
    ) {
        return false;
    }
    // The smooth-position target is transient (released after the adjust
    // pass); the delta diagnostics below only run while it still exists.
    const bool have_smooth =
        m_smooth_position_texture &&
        read_rgba_texture_to_float(m_graphics_device, *m_smooth_position_texture, smooth_data);
    if (have_smooth) {
        // Terminator-fix diagnostics: how far the Phong-tessellated smooth
        // position moves off the flat surface (zero on flat-shaded charts).
        std::size_t covered = 0;
        std::size_t moved   = 0;
        double      sum     = 0.0;
        float       peak    = 0.0f;
        for (std::size_t i = 0; i < texel_count; ++i) {
            if (position_data[i * 4 + 3] <= 0.0f) {
                continue;
            }
            ++covered;
            const glm::vec3 flat_p  {position_data[i * 4 + 0], position_data[i * 4 + 1], position_data[i * 4 + 2]};
            const glm::vec3 smooth_p{smooth_data  [i * 4 + 0], smooth_data  [i * 4 + 1], smooth_data  [i * 4 + 2]};
            const float delta = glm::length(smooth_p - flat_p);
            if (delta > 1.0e-6f) {
                ++moved;
            }
            sum  = sum + delta;
            peak = std::max(peak, delta);
        }
        log_render->info(
            "Lightmap_baker: smooth-position delta over {} covered texels: moved {} ({:.1f}%), mean {:.6f} m, max {:.6f} m",
            covered, moved, covered > 0 ? 100.0 * static_cast<double>(moved) / static_cast<double>(covered) : 0.0,
            covered > 0 ? sum / static_cast<double>(covered) : 0.0, peak
        );
        for (const Instance_region& region : m_layout.regions) {
            if (region.tile != m_gbuffer_tile) {
                continue;
            }
            std::size_t region_covered = 0;
            std::size_t region_moved   = 0;
            float       region_peak    = 0.0f;
            for (int y = region.y; y < region.y + region.height; ++y) {
                for (int x = region.x; x < region.x + region.width; ++x) {
                    const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
                    if (position_data[i * 4 + 3] <= 0.0f) {
                        continue;
                    }
                    ++region_covered;
                    const glm::vec3 flat_p  {position_data[i * 4 + 0], position_data[i * 4 + 1], position_data[i * 4 + 2]};
                    const glm::vec3 smooth_p{smooth_data  [i * 4 + 0], smooth_data  [i * 4 + 1], smooth_data  [i * 4 + 2]};
                    const float delta = glm::length(smooth_p - flat_p);
                    if (delta > 1.0e-6f) {
                        ++region_moved;
                    }
                    region_peak = std::max(region_peak, delta);
                }
            }
            log_render->info(
                "  region '{}' {}x{}: covered {}, moved {}, max {:.6f} m",
                region.mesh ? region.mesh->get_name() : "?", region.width, region.height, region_covered, region_moved, region_peak
            );
        }
    }
    std::vector<float> albedo_data;
    if (!read_rgba_texture_to_float(m_graphics_device, *m_albedo_texture, albedo_data)) {
        return false;
    }

    // Positions map into the covered world bounds so the PNG uses the full
    // 8-bit range; normals map as n * 0.5 + 0.5. Alpha = coverage.
    glm::vec3 minimum{std::numeric_limits<float>::max()};
    glm::vec3 maximum{std::numeric_limits<float>::lowest()};
    for (std::size_t i = 0; i < texel_count; ++i) {
        if (position_data[i * 4 + 3] <= 0.0f) {
            continue;
        }
        for (int c = 0; c < 3; ++c) {
            minimum[c] = std::min(minimum[c], position_data[i * 4 + c]);
            maximum[c] = std::max(maximum[c], position_data[i * 4 + c]);
        }
    }
    const glm::vec3 extent = glm::max(maximum - minimum, glm::vec3{1e-6f});

    std::unique_ptr<Image_writer> writer = Image_writer::create();
    if (!writer || !writer->is_supported()) {
        return false;
    }
    const auto write_image = [&](const std::string& path, const std::vector<float>& data, const bool is_position) -> bool {
        std::vector<std::uint8_t> pixels(texel_count * 4u);
        for (std::size_t i = 0; i < texel_count; ++i) {
            const float coverage = data[i * 4 + 3];
            for (int c = 0; c < 3; ++c) {
                const float value = is_position
                    ? (data[i * 4 + c] - minimum[c]) / extent[c]
                    : data[i * 4 + c] * 0.5f + 0.5f;
                pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            }
            pixels[i * 4 + 3] = (coverage > 0.0f) ? 255u : 0u;
        }
        return writer->write_png(
            std::filesystem::path{path},
            width,
            height,
            width * 4,
            erhe::dataformat::Format::format_8_vec4_unorm,
            std::span<const std::byte>{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()}
        );
    };
    const bool position_ok = write_image(base_path + "_position.png", position_data, true);
    const bool normal_ok   = write_image(base_path + "_normal.png",   normal_data,   false);
    // Albedo is already 0..1; reuse the normal mapping's identity-ish path
    // by pre-biasing so value * 0.5 + 0.5 becomes value.
    std::vector<float> albedo_biased(albedo_data.size());
    for (std::size_t i = 0; i < albedo_data.size(); ++i) {
        albedo_biased[i] = ((i % 4u) == 3u) ? albedo_data[i] : albedo_data[i] * 2.0f - 1.0f;
    }
    const bool albedo_ok = write_image(base_path + "_albedo.png", albedo_biased, false);
    return position_ok && normal_ok && albedo_ok;
}

auto Lightmap_baker::get_or_create_blas(
    erhe::graphics::Command_buffer&                    command_buffer,
    const std::shared_ptr<erhe::primitive::Primitive>& primitive,
    const erhe::primitive::Buffer_mesh&                buffer_mesh
) -> erhe::graphics::Acceleration_structure*
{
    using namespace erhe::graphics;

    const auto existing = m_blas_cache.find(&buffer_mesh);
    if (existing != m_blas_cache.end()) {
        return existing->second.acceleration_structure.get();
    }
    // Mirrors Ray_trace_renderer::get_or_create_blas: triangles read in
    // place from the mesh memory pools (stream 0 leads with position,
    // uint32 triangle-list indices relative to the range start).
    if (buffer_mesh.vertex_buffer_ranges.empty()) {
        return nullptr;
    }
    const erhe::primitive::Buffer_range& vertex_range = buffer_mesh.vertex_buffer_ranges[0];
    const erhe::primitive::Buffer_range& index_range  = buffer_mesh.index_buffer_range;
    const erhe::primitive::Index_range&  triangles    = buffer_mesh.triangle_fill_indices;
    if ((triangles.index_count == 0) || (triangles.primitive_type != erhe::primitive::Primitive_type::triangles)) {
        return nullptr;
    }
    if ((vertex_range.count == 0) || (index_range.element_size != sizeof(uint32_t))) {
        return nullptr;
    }
    erhe::graphics::Buffer* vertex_buffer = m_mesh_memory.get_vertex_buffer(vertex_range);
    erhe::graphics::Buffer* index_buffer  = m_mesh_memory.get_index_buffer(index_range);
    if ((vertex_buffer == nullptr) || (index_buffer == nullptr)) {
        return nullptr;
    }

    Blas_entry& entry = m_blas_cache[&buffer_mesh];
    entry.primitive = primitive;
    entry.acceleration_structure = std::make_unique<Acceleration_structure>(
        m_graphics_device,
        Acceleration_structure_create_info{
            .type                = Acceleration_structure_type::bottom_level,
            .triangle_geometries = {
                Acceleration_structure_triangles{
                    .vertex_buffer      = vertex_buffer,
                    .vertex_byte_offset = vertex_range.byte_offset,
                    .vertex_byte_stride = vertex_range.element_size,
                    .vertex_count       = vertex_range.count,
                    .index_buffer       = index_buffer,
                    .index_byte_offset  = index_range.byte_offset + (triangles.first_index * index_range.element_size),
                    .index_count        = triangles.index_count,
                    .opaque             = true
                }
            },
            .debug_label = erhe::utility::Debug_label{"Lightmap BLAS"}
        }
    );
    entry.acceleration_structure->build(command_buffer);
    return entry.acceleration_structure.get();
}

void Lightmap_baker::ensure_bake_targets(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;
    const auto texture_matches = [this](const std::shared_ptr<Texture>& texture) {
        return texture && (texture->get_width() == m_layout.width) && (texture->get_height() == m_layout.height);
    };
    const auto make_storage = [this](const char* label, const erhe::dataformat::Format format, const int width, const int height, const uint64_t usage_mask) {
        return std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  = usage_mask,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = width,
                .height      = height,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    };
    // Working atlas + dilate scratch are CELL-sized (one tile resolves /
    // publishes at a time); the renderer-facing display atlas is the full
    // page.
    const int tile_size = m_layout.get_tile_size();
    const bool working_matches =
        m_lightmap_texture &&
        (m_lightmap_texture->get_width()  == tile_size) &&
        (m_lightmap_texture->get_height() == tile_size);
    if (!working_matches) {
        m_lightmap_texture = make_storage(
            "lightmap working atlas",
            c_atlas_format,
            tile_size,
            tile_size,
            Image_usage_flag_bit_mask::storage          |
            Image_usage_flag_bit_mask::sampled          |
            Image_usage_flag_bit_mask::color_attachment | // seam blend line raster target
            Image_usage_flag_bit_mask::transfer_src     |
            Image_usage_flag_bit_mask::transfer_dst
        );
        // The dilate scratch doubles as the seam pass sample source (a copy
        // of the working atlas, avoiding a read/write hazard like Godot's
        // light_accum_tex2).
        m_dilate_texture = make_storage(
            "lightmap dilate scratch",
            c_atlas_format,
            tile_size,
            tile_size,
            Image_usage_flag_bit_mask::storage      |
            Image_usage_flag_bit_mask::sampled      |
            Image_usage_flag_bit_mask::transfer_src | // white source for fresh-slot clears
            Image_usage_flag_bit_mask::transfer_dst
        );
        command_buffer.clear_texture(*m_lightmap_texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    }
    // Renderer-facing double buffer: only complete publishes are copied
    // in, so accumulation resets never black out the sampled atlas. It
    // also survives a working-set release (set_baking_enabled(false)), so
    // recreate only when missing or the page size changed.
    if (!texture_matches(m_display_texture)) {
        m_display_texture = make_storage(
            "lightmap display atlas",
            c_atlas_format,
            m_layout.width,
            m_layout.height,
            Image_usage_flag_bit_mask::sampled      |
            Image_usage_flag_bit_mask::transfer_src |
            Image_usage_flag_bit_mask::transfer_dst
        );
        m_display_cleared = false;
    }
    if (!m_display_cleared) {
        // Runs on creation and layout repacks (update_layout drops
        // m_display_cleared: regions moved, so the previous publish no
        // longer matches the uv_scale_offsets pushed to the meshes).
        // Lighting/transform resets keep the display showing the last
        // publish while accumulation rebuilds. White = the unbaked look
        // (matches the piece white-fallback sentinel); alpha 0 keeps "no
        // coverage" for the atlas readback consumers.
        command_buffer.clear_texture(*m_display_texture, {1.0, 1.0, 1.0, 0.0});
        command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
        m_display_cleared = true;
        m_slots_pending_white_clear.clear(); // the full-page clear covers them
        for (Tile_state& tile : m_tiles) {
            tile.published = false;
        }
    }
}

void Lightmap_baker::record_pending_slot_white_clears(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;
    if (m_slots_pending_white_clear.empty() || !m_display_texture || !m_dilate_texture) {
        return;
    }
    // A freshly assigned display slot still holds the PREVIOUS occupant
    // tile's published texels, and the new occupant's regions map into it
    // immediately (publish_regions on the residency change) - stale
    // lighting until its first publish/restore. Overwrite the slot with
    // white (the unbaked look) using the tile-sized dilate scratch as the
    // source (it is re-filled by every dilate/seam pass, so clobbering it
    // here is safe).
    const int tile_size = m_layout.get_tile_size();
    command_buffer.clear_texture(*m_dilate_texture, {1.0, 1.0, 1.0, 0.0});
    command_buffer.transition_texture_layout(*m_dilate_texture,  Image_layout::transfer_src_optimal);
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        for (const int slot : m_slots_pending_white_clear) {
            const glm::ivec2 slot_origin = m_layout.get_slot_origin(slot);
            blit.copy_from_texture(
                m_dilate_texture.get(),
                0, 0,
                glm::ivec3{0, 0, 0},
                glm::ivec3{tile_size, tile_size, 1},
                m_display_texture.get(),
                0, 0,
                glm::ivec3{slot_origin.x, slot_origin.y, 0}
            );
        }
    }
    command_buffer.transition_texture_layout(*m_dilate_texture,  Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
    m_slots_pending_white_clear.clear();
}

auto Lightmap_baker::ensure_tile_accum(erhe::graphics::Command_buffer& command_buffer, const int tile) -> erhe::graphics::Texture*
{
    using namespace erhe::graphics;
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size()))) {
        return nullptr;
    }
    Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
    const int tile_size = m_layout.get_tile_size();
    const bool matches =
        state.accum &&
        (state.accum->get_width()  == tile_size) &&
        (state.accum->get_height() == tile_size);
    if (!matches) {
        state.accum = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    Image_usage_flag_bit_mask::storage |
                    Image_usage_flag_bit_mask::transfer_dst,
                .type        = Texture_type::texture_2d,
                .pixelformat = c_accum_format,
                .width       = tile_size,
                .height      = tile_size,
                .debug_label = erhe::utility::Debug_label{"lightmap tile accumulation"}
            }
        );
        state.accum_dirty = true;
    }
    if (state.accum_dirty) {
        command_buffer.clear_texture(*state.accum, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*state.accum, Image_layout::general);
        state.accum_dirty = false;
        state.sweeps      = 0;
    }
    return state.accum.get();
}

void Lightmap_baker::publish_regions()
{
    // Per-primitive atlas regions so the forward renderer samples the bake
    // (Primitive_buffer uploads the value per draw; zero = no lightmap).
    // The display mapping goes through the region's tile's current slot:
    // non-resident tiles publish zero and render unlit until they regain a
    // slot (re-published on every residency change).
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh) {
            continue;
        }
        // Through the Mesh setter so the draw list primitive records follow.
        region.mesh->set_primitive_lightmap_uv_scale_offset(region.primitive_index, m_layout.display_uv_scale_offset(region));
    }
    m_regions_published = true;
}

auto Lightmap_baker::collect_lights(Scene_root& scene_root) const -> std::vector<Light_record>
{
    std::vector<Light_record> lights;
    for (const std::shared_ptr<erhe::scene::Light>& light : scene_root.layers().light()->lights) {
        if (!light || !light->is_visible() || (lights.size() >= c_max_gather_lights)) {
            continue;
        }
        const erhe::scene::Node* const node = light->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        // Light direction convention matches Light_buffer: node +Z axis.
        const glm::vec3 direction = glm::normalize(glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 1.0f, 0.0f}});
        const glm::vec3 position  = glm::vec3{world_from_node * glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}};
        const glm::vec3 radiance  = light->intensity * light->get_effective_color();
        float type_value = 0.0f;
        switch (light->type) {
            case erhe::scene::Light_type::directional: type_value = 0.0f; break;
            case erhe::scene::Light_type::point:       type_value = 1.0f; break;
            case erhe::scene::Light_type::spot:        type_value = 2.0f; break;
            default: continue;
        }
        lights.push_back(
            Light_record{
                .position_and_type       = glm::vec4{position, type_value},
                .direction_and_outer_cos = glm::vec4{direction, std::cos(light->outer_spot_angle * 0.5f)},
                .radiance_and_range      = glm::vec4{radiance, light->range},
                .params                  = glm::vec4{std::cos(light->inner_spot_angle * 0.5f), 0.0f, 0.0f, 0.0f}
            }
        );
    }
    return lights;
}

void Lightmap_baker::write_gather_ubo(
    std::byte* const                 data,
    const std::vector<Light_record>& lights,
    const uint32_t                   frame_index,
    const uint32_t                   base_texel_y
) const
{
    std::memset(data, 0, m_gather_block_size);
    const uint32_t light_count = static_cast<uint32_t>(lights.size());
    // Unused since the article leak defenses (adaptive bias + virtual
    // offset in the shader); kept as block padding.
    const float    ray_bias    = 0.0f;
    std::memcpy(data + m_gather_light_count_offset, &light_count,  sizeof(uint32_t));
    std::memcpy(data + m_gather_ray_bias_offset,    &ray_bias,     sizeof(float));
    std::memcpy(data + m_gather_frame_index_offset, &frame_index,  sizeof(uint32_t));
    std::memcpy(data + m_gather_base_y_offset,      &base_texel_y, sizeof(uint32_t));
    const uint32_t bounce_enabled = m_options.indirect_bounce ? 1u : 0u;
    std::memcpy(data + m_gather_bounce_enabled_offset, &bounce_enabled, sizeof(uint32_t));
    // Sky is only enabled when both LUTs are actually bound (see the
    // placeholder-binding comment in the gather layout).
    const uint32_t sky_enabled =
        (m_sky.enabled && (m_sky.transmittance_lut != nullptr) && (m_sky.multiscatter_lut != nullptr)) ? 1u : 0u;
    std::memcpy(data + m_gather_sky_enabled_offset,   &sky_enabled,                     sizeof(uint32_t));
    std::memcpy(data + m_gather_sun_direction_offset, &m_sky.sun_direction_and_intensity, sizeof(glm::vec4));
    std::memcpy(data + m_gather_sky_params_offset,    &m_sky.sky_params,                  sizeof(glm::vec4));
    for (std::size_t i = 0; i < lights.size(); ++i) {
        std::memcpy(data + m_gather_position_type_offset  + i * sizeof(glm::vec4), &lights[i].position_and_type,       sizeof(glm::vec4));
        std::memcpy(data + m_gather_direction_cos_offset  + i * sizeof(glm::vec4), &lights[i].direction_and_outer_cos, sizeof(glm::vec4));
        std::memcpy(data + m_gather_radiance_range_offset + i * sizeof(glm::vec4), &lights[i].radiance_and_range,      sizeof(glm::vec4));
        std::memcpy(data + m_gather_params_offset         + i * sizeof(glm::vec4), &lights[i].params,                  sizeof(glm::vec4));
    }
}

void Lightmap_baker::collect_instances(
    erhe::graphics::Command_buffer&                                command_buffer,
    Scene_root&                                                    scene_root,
    std::vector<erhe::graphics::Acceleration_structure_instance>&  out_instances,
    std::vector<Lm_instance_record>&                               out_records
)
{
    using namespace erhe::graphics;

    out_instances.clear();
    out_records.clear();

    // Shadow world: every visible, non-skinned content mesh occludes,
    // whether or not it is lightmapped. Lightmapped primitives additionally
    // carry their atlas region so bounce rays can look up published
    // radiance + albedo at the hit point. proxy_hidden sources are skipped:
    // their render proxies (the piece meshes) are the occluders - both at
    // once would put coplanar duplicates in every shadow ray.
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::proxy_hidden) != 0u) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        const std::vector<erhe::scene::Mesh_primitive>& primitives = mesh->get_primitives();
        for (std::size_t primitive_index = 0; primitive_index < primitives.size(); ++primitive_index) {
            const erhe::scene::Mesh_primitive& mesh_primitive = primitives[primitive_index];
            if (!mesh_primitive.primitive) {
                continue;
            }
            const erhe::primitive::Buffer_mesh* buffer_mesh = mesh_primitive.primitive->get_renderable_mesh();
            if (buffer_mesh == nullptr) {
                continue;
            }
            Acceleration_structure* blas = get_or_create_blas(command_buffer, mesh_primitive.primitive, *buffer_mesh);
            if (blas == nullptr) {
                continue;
            }

            Lm_instance_record record{};
            {
                // Index + stream-0 position addresses for EVERY occluder:
                // the committed_face_normal position-fetch fallback reads
                // them for any hit instance, lightmapped or not. The ranges
                // are the same ones the BLAS above was built from, so a
                // non-null blas guarantees they exist.
                const erhe::primitive::Buffer_range& position_range = buffer_mesh->vertex_buffer_ranges[0];
                const erhe::primitive::Buffer_range& index_range    = buffer_mesh->index_buffer_range;
                erhe::graphics::Buffer* position_buffer = m_mesh_memory.get_vertex_buffer(position_range);
                erhe::graphics::Buffer* index_buffer    = m_mesh_memory.get_index_buffer(index_range);
                if ((position_buffer == nullptr) || (index_buffer == nullptr) || ((position_range.element_size % 4) != 0)) {
                    continue;
                }
                const uint64_t position_base_address = position_buffer->get_device_address();
                const uint64_t index_base_address    = index_buffer->get_device_address();
                if ((position_base_address == 0) || (index_base_address == 0)) {
                    continue;
                }
                record.index_address         = index_base_address + index_range.byte_offset + (buffer_mesh->triangle_fill_indices.first_index * index_range.element_size);
                record.position_address      = position_base_address + position_range.byte_offset;
                record.position_stride_uints = static_cast<uint32_t>(position_range.element_size / 4);
            }
            if (buffer_mesh->vertex_buffer_ranges.size() >= 2) {
                const erhe::primitive::Buffer_range& attribute_range = buffer_mesh->vertex_buffer_ranges[1];
                erhe::graphics::Buffer* attribute_buffer = m_mesh_memory.get_vertex_buffer(attribute_range);
                if ((attribute_buffer != nullptr) && ((attribute_range.element_size % 4) == 0)) {
                    const uint64_t attribute_base_address = attribute_buffer->get_device_address();
                    if (attribute_base_address != 0) {
                        record.vertex_address      = attribute_base_address + attribute_range.byte_offset;
                        record.vertex_stride_uints = static_cast<uint32_t>(attribute_range.element_size / 4);
                        for (const Instance_region& region : m_layout.regions) {
                            if ((region.mesh == mesh) && (region.primitive_index == primitive_index)) {
                                // Bounce rays sample the display atlas, so
                                // the record needs the display (slot-space)
                                // mapping; zero when the tile is not
                                // resident (bounce contributes nothing).
                                // The partitioned-mode white-fallback
                                // sentinel (scale.x < 0) is for the forward
                                // renderer only - white here would inject
                                // fake energy into the gather, so bounce
                                // stays black for non-resident tiles.
                                record.uv_scale_offset = m_layout.display_uv_scale_offset(region);
                                if (record.uv_scale_offset.x < 0.0f) {
                                    record.uv_scale_offset = glm::vec4{0.0f};
                                }
                                break;
                            }
                        }
                    }
                }
            }
            out_records.push_back(record);
            out_instances.push_back(
                Acceleration_structure_instance{
                    .transform             = world_from_node,
                    .instance_custom_index = static_cast<uint32_t>(out_instances.size()),
                    .mask                  = 0xFFu,
                    .bottom_level          = blas,
                    // Shadow rays cull backfaces via ray flags (article
                    // leak defenses); disabling facing cull per instance
                    // would defeat that, so keep the default.
                    .disable_facing_cull   = false
                }
            );
        }
    }
}

auto Lightmap_baker::bake_direct(Scene_root& scene_root) -> bool
{
    using namespace erhe::graphics;

    if (!m_gather_pipeline || !m_resolve_pipeline || (m_layout.width == 0) || m_tiles.empty()) {
        return false;
    }

    const std::vector<Light_record> lights = collect_lights(scene_root);

    m_direct_gather_ubo = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = m_gather_block_size,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::uniform,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap gather ubo"}
        }
    );
    {
        const std::span<std::byte> mapped = m_direct_gather_ubo->map_bytes(0, m_gather_block_size);
        write_gather_ubo(mapped.data(), lights, 0u, 0u);
        m_direct_gather_ubo->unmap();
    }

    // One tile at a time: raster the tile's G-buffer (standalone submit
    // inside bake_gbuffer), then gather + resolve + publish the tile in a
    // second standalone submit. One-shot bake semantics per tile: restart
    // the tile's accumulation so the result is exactly one full sample -
    // direct light plus one bounce off whatever the display atlas held.
    const int    tile_size  = m_layout.get_tile_size();
    std::size_t  tiles_done = 0;
    constexpr unsigned int bake_thread_slot = 6;
    for (int tile = 0; tile < m_layout.get_tile_count(); ++tile) {
        if (!m_tiles[static_cast<std::size_t>(tile)].has_content) {
            continue;
        }
        // One-shot bakes cover the resident tiles only (they are the ones
        // with a display slot to publish into); the offline bake-to-disk
        // path is the way to bake every tile of a large world.
        if (m_layout.tiles[static_cast<std::size_t>(tile)].slot < 0) {
            continue;
        }
        if (!bake_gbuffer(tile)) {
            continue;
        }

        Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
        command_buffer.begin();
        ensure_bake_targets(command_buffer);
        m_tiles[static_cast<std::size_t>(tile)].accum_dirty = true;
        Texture* const accum = ensure_tile_accum(command_buffer, tile);
        if (accum == nullptr) {
            command_buffer.end();
            continue;
        }

        collect_instances(command_buffer, scene_root, m_tick_instances, m_tick_records);
        if (m_tick_instances.empty()) {
            command_buffer.end();
            return false;
        }
        const std::size_t record_byte_count = m_tick_records.size() * sizeof(Lm_instance_record);
        m_direct_instance_ssbo = std::make_unique<Buffer>(
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = record_byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::storage,
                .required_memory_property_bit_mask      =
                    Memory_property_flag_bit_mask::host_read |
                    Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     =
                    Memory_property_flag_bit_mask::host_coherent |
                    Memory_property_flag_bit_mask::host_persistent,
                .debug_label = erhe::utility::Debug_label{"lightmap instance records"}
            }
        );
        {
            const std::span<std::byte> mapped = m_direct_instance_ssbo->map_bytes(0, record_byte_count);
            std::memcpy(mapped.data(), m_tick_records.data(), record_byte_count);
            m_direct_instance_ssbo->unmap();
        }

        const uint32_t required_capacity = static_cast<uint32_t>(m_tick_instances.size());
        if (!m_tlas || (m_tlas_capacity < required_capacity)) {
            const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
            m_tlas = std::make_unique<Acceleration_structure>(
                m_graphics_device,
                Acceleration_structure_create_info{
                    .type               = Acceleration_structure_type::top_level,
                    .max_instance_count = new_capacity,
                    .debug_label        = erhe::utility::Debug_label{"Lightmap TLAS"}
                }
            );
            m_tlas_capacity = new_capacity;
        }
        m_tlas->build(command_buffer, m_tick_instances);

        // Virtual offset (one-shot per G-buffer): needs the TLAS just built.
        record_adjust(
            command_buffer,
            *m_tlas,
            [&](Compute_command_encoder& encoder) {
                encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count, 1);
            }
        );

        // Gather one full-tile sample: the display atlas is sampled by
        // bounce rays (shader_read_only), the tile accumulation image is
        // written (general).
        command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
        {
            Compute_pipeline* const ss_pipeline =
                (m_origin_factor == 8) ? m_gather_ss8_pipeline.get() :
                (m_origin_factor == 4) ? m_gather_ss4_pipeline.get() : nullptr;
            const bool supersample = m_origin_valid && m_origin_texture && (ss_pipeline != nullptr);
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_gather_layout.get());
            encoder.set_compute_pipeline(supersample ? *ss_pipeline : *m_gather_pipeline);
            encoder.set_buffer(Buffer_target::uniform, m_direct_gather_ubo.get(),    0, m_gather_block_size, 0);
            encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count,   1);
            encoder.set_acceleration_structure(2u, *m_tlas);
            encoder.set_sampled_image(3u, *m_position_texture, *m_nearest_sampler);
            encoder.set_sampled_image(4u, *m_normal_texture,   *m_nearest_sampler);
            encoder.set_sampled_image(5u, *m_albedo_texture,   *m_linear_sampler);
            encoder.set_sampled_image(6u, *m_display_texture,  *m_linear_sampler);
            encoder.set_sampled_image(7u, (m_sky.transmittance_lut != nullptr) ? *m_sky.transmittance_lut : *m_albedo_texture, *m_linear_sampler);
            encoder.set_sampled_image(8u, (m_sky.multiscatter_lut  != nullptr) ? *m_sky.multiscatter_lut  : *m_albedo_texture, *m_linear_sampler);
            encoder.set_sampled_image(9u, supersample ? *m_origin_texture : *m_position_texture, *m_nearest_sampler);
            encoder.set_storage_image(12u, *accum);
            encoder.dispatch_compute(
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                1
            );
        }

        record_resolve_and_dilate(command_buffer, tile, false);
        record_display_publish(command_buffer, tile);
        command_buffer.end();
        Command_buffer* command_buffers[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
        m_graphics_device.wait_idle();
        m_tiles[static_cast<std::size_t>(tile)].published        = true;
        m_tiles[static_cast<std::size_t>(tile)].sweeps           = 1;
        m_tiles[static_cast<std::size_t>(tile)].dirty_since_save = true;
        ++tiles_done;
    }
    if (tiles_done == 0) {
        return false;
    }

    m_lightmap_valid = true;
    m_cursor_tile    = 0;
    m_cursor_y       = 0;
    publish_regions();

    log_render->info(
        "Lightmap_baker: direct light baked, {} lights, {} tiles, {}x{} page",
        lights.size(), tiles_done, m_layout.width, m_layout.height
    );
    return true;
}

auto Lightmap_baker::get_bake_parameters_hash() const -> uint64_t
{
    uint64_t hash = fnv1a64(&m_tile_size, sizeof(int));
    hash = fnv1a64(&m_cell_size, sizeof(float), hash);
    const uint32_t option_bits =
        (m_options.indirect_bounce ? 1u  : 0u) |
        (m_options.terminator_fix  ? 2u  : 0u) |
        (m_options.denoise         ? 4u  : 0u) |
        (m_options.dilation        ? 8u  : 0u) |
        (m_options.seam_blend      ? 16u : 0u) |
        (static_cast<uint32_t>(m_options.coverage_mode)      << 5) |
        (static_cast<uint32_t>(m_options.supersample_factor) << 8);
    hash = fnv1a64(&option_bits, sizeof(uint32_t), hash);
    return hash;
}

auto Lightmap_baker::start_offline_bake(Scene_root& scene_root, const uint32_t target_sweeps, Offline_tile_sink sink) -> bool
{
    static_cast<void>(scene_root);
    if (m_offline_state.progress.active || !m_gather_pipeline || !m_resolve_pipeline || !sink) {
        return false;
    }
    if (m_layout.get_tile_count() == 0) {
        if (m_report != nullptr) {
            m_report->add_error(Lightmap_report::Stage::bake, "offline bake", "no atlas layout - run Update Atlas Layout first");
        }
        return false;
    }
    m_offline_state.progress = Offline_progress{
        .active        = true,
        .tiles_done    = 0,
        .tile_count    = m_layout.get_tile_count(),
        .target_sweeps = std::max(1u, target_sweeps)
    };
    m_offline_state.next_tile = 0;
    m_offline_state.sink      = std::move(sink);
    log_render->info(
        "Lightmap_baker: offline bake started, {} tiles of {}^2, {} sweeps per tile",
        m_layout.get_tile_count(), m_layout.get_tile_size(), m_offline_state.progress.target_sweeps
    );
    return true;
}

void Lightmap_baker::cancel_offline_bake()
{
    if (!m_offline_state.progress.active) {
        return;
    }
    m_offline_state.progress.active = false;
    m_offline_state.sink            = {};
    log_render->info("Lightmap_baker: offline bake cancelled");
}

auto Lightmap_baker::offline_tick(Scene_root& scene_root) -> bool
{
    using namespace erhe::graphics;
    Offline_state& offline = m_offline_state;
    if (!offline.progress.active) {
        return false;
    }
    const int tile = offline.next_tile;
    if (tile >= m_layout.get_tile_count()) {
        offline.progress.active = false;
        offline.sink            = {};
        log_render->info("Lightmap_baker: offline bake finished, {} tiles", offline.progress.tiles_done);
        return false;
    }
    const auto fail = [this, &offline, tile](const std::string& message) -> bool {
        if (m_report != nullptr) {
            m_report->add_error(Lightmap_report::Stage::bake, fmt::format("tile {}", tile), message);
        }
        log_render->warn("Lightmap_baker: offline bake aborted at tile {}: {}", tile, message);
        offline.progress.active = false;
        offline.sink            = {};
        return false;
    };

    // 1. Tile G-buffer (standalone submit inside).
    if (!bake_gbuffer(tile)) {
        return fail("G-buffer raster failed");
    }

    const std::vector<Light_record> lights    = collect_lights(scene_root);
    const int                       tile_size = m_layout.get_tile_size();
    constexpr unsigned int          bake_thread_slot = 6;

    // 2. target_sweeps full-tile gather submits into this tile's fresh
    // accumulation. One submit + wait_idle per sweep so the single plain
    // gather UBO can be rewritten between sweeps (frame_index decorrelates
    // the RNG per sweep).
    m_direct_gather_ubo = std::make_unique<Buffer>(
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = m_gather_block_size,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::uniform,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap offline gather ubo"}
        }
    );
    std::size_t record_byte_count = 0;
    Texture*    accum             = nullptr;
    for (uint32_t sweep = 0; sweep < offline.progress.target_sweeps; ++sweep) {
        {
            const std::span<std::byte> mapped = m_direct_gather_ubo->map_bytes(0, m_gather_block_size);
            write_gather_ubo(mapped.data(), lights, sweep, 0u);
            m_direct_gather_ubo->unmap();
        }
        Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
        command_buffer.begin();
        if (sweep == 0) {
            ensure_bake_targets(command_buffer);
            m_tiles[static_cast<std::size_t>(tile)].accum_dirty = true;
            accum = ensure_tile_accum(command_buffer, tile);
            if (accum == nullptr) {
                command_buffer.end();
                return fail("accumulation target unavailable");
            }
            collect_instances(command_buffer, scene_root, m_tick_instances, m_tick_records);
            if (m_tick_instances.empty()) {
                command_buffer.end();
                return fail("no occluder instances (empty scene?)");
            }
            record_byte_count = m_tick_records.size() * sizeof(Lm_instance_record);
            m_direct_instance_ssbo = std::make_unique<Buffer>(
                m_graphics_device,
                Buffer_create_info{
                    .capacity_byte_count                    = record_byte_count,
                    .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                    .usage                                  = Buffer_usage::storage,
                    .required_memory_property_bit_mask      =
                        Memory_property_flag_bit_mask::host_read |
                        Memory_property_flag_bit_mask::host_write,
                    .preferred_memory_property_bit_mask     =
                        Memory_property_flag_bit_mask::host_coherent |
                        Memory_property_flag_bit_mask::host_persistent,
                    .debug_label = erhe::utility::Debug_label{"lightmap offline instance records"}
                }
            );
            {
                const std::span<std::byte> mapped = m_direct_instance_ssbo->map_bytes(0, record_byte_count);
                std::memcpy(mapped.data(), m_tick_records.data(), record_byte_count);
                m_direct_instance_ssbo->unmap();
            }
            const uint32_t required_capacity = static_cast<uint32_t>(m_tick_instances.size());
            if (!m_tlas || (m_tlas_capacity < required_capacity)) {
                const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
                m_tlas = std::make_unique<Acceleration_structure>(
                    m_graphics_device,
                    Acceleration_structure_create_info{
                        .type               = Acceleration_structure_type::top_level,
                        .max_instance_count = new_capacity,
                        .debug_label        = erhe::utility::Debug_label{"Lightmap TLAS"}
                    }
                );
                m_tlas_capacity = new_capacity;
            }
            m_tlas->build(command_buffer, m_tick_instances);
            record_adjust(
                command_buffer,
                *m_tlas,
                [&](Compute_command_encoder& encoder) {
                    encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count, 1);
                }
            );
        }
        command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
        {
            Compute_pipeline* const ss_pipeline =
                (m_origin_factor == 8) ? m_gather_ss8_pipeline.get() :
                (m_origin_factor == 4) ? m_gather_ss4_pipeline.get() : nullptr;
            const bool supersample = m_origin_valid && m_origin_texture && (ss_pipeline != nullptr);
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_gather_layout.get());
            encoder.set_compute_pipeline(supersample ? *ss_pipeline : *m_gather_pipeline);
            encoder.set_buffer(Buffer_target::uniform, m_direct_gather_ubo.get(),    0, m_gather_block_size, 0);
            encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count,   1);
            encoder.set_acceleration_structure(2u, *m_tlas);
            encoder.set_sampled_image(3u, *m_position_texture, *m_nearest_sampler);
            encoder.set_sampled_image(4u, *m_normal_texture,   *m_nearest_sampler);
            encoder.set_sampled_image(5u, *m_albedo_texture,   *m_linear_sampler);
            encoder.set_sampled_image(6u, *m_display_texture,  *m_linear_sampler);
            encoder.set_sampled_image(7u, (m_sky.transmittance_lut != nullptr) ? *m_sky.transmittance_lut : *m_albedo_texture, *m_linear_sampler);
            encoder.set_sampled_image(8u, (m_sky.multiscatter_lut  != nullptr) ? *m_sky.multiscatter_lut  : *m_albedo_texture, *m_linear_sampler);
            encoder.set_sampled_image(9u, supersample ? *m_origin_texture : *m_position_texture, *m_nearest_sampler);
            encoder.set_storage_image(12u, *accum);
            encoder.dispatch_compute(
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                1
            );
        }
        const bool last_sweep = (sweep + 1) == offline.progress.target_sweeps;
        if (last_sweep) {
            // 3. Resolve + optional denoise + dilate + seam blend into the
            // tile-sized working atlas, and (when this tile happens to hold
            // a display slot) publish so the viewport reflects the bake.
            record_resolve_and_dilate(command_buffer, tile, m_options.denoise);
            record_display_publish(command_buffer, tile);
        }
        command_buffer.end();
        Command_buffer* command_buffers[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
        m_graphics_device.wait_idle();
    }
    {
        Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
        state.sweeps           = offline.progress.target_sweeps;
        state.dirty_since_save = false; // the offline sink just persisted it
        state.published = m_layout.tiles[static_cast<std::size_t>(tile)].slot >= 0;
    }
    m_lightmap_valid = true;

    // 4. CPU readback of the tile-sized working atlas -> fp16 -> sink.
    {
        std::vector<float> pixels;
        if (!m_lightmap_texture || !read_rgba_texture_to_float(m_graphics_device, *m_lightmap_texture, pixels)) {
            return fail("working atlas readback failed");
        }
        std::vector<uint16_t> half_pixels(pixels.size());
        for (std::size_t i = 0; i < pixels.size(); ++i) {
            half_pixels[i] = Lightmap_tile_io::float_to_half(pixels[i]);
        }
        if (!offline.sink(tile, tile_size, tile_size, half_pixels)) {
            return fail("tile sink failed (disk write?)");
        }
    }

    // 5. Release this tile's accumulation - only one tile's working set is
    // ever resident during an offline bake (destruction is deferred behind
    // the in-flight frames; wait_idle above already drained them).
    {
        Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
        state.accum.reset();
        state.accum_dirty = true;
    }

    ++offline.progress.tiles_done;
    ++offline.next_tile;
    if (offline.next_tile >= m_layout.get_tile_count()) {
        offline.progress.active = false;
        offline.sink            = {};
        log_render->info("Lightmap_baker: offline bake finished, {} tiles", offline.progress.tiles_done);
        return false;
    }
    return true;
}

void Lightmap_baker::record_adjust(
    erhe::graphics::Command_buffer&                                       command_buffer,
    erhe::graphics::Acceleration_structure&                               tlas,
    const std::function<void(erhe::graphics::Compute_command_encoder&)>&  bind_instance_records
)
{
    using namespace erhe::graphics;
    Compute_pipeline* const adjust_pipeline = m_options.terminator_fix ? m_adjust_pipeline.get() : m_adjust_no_smooth_pipeline.get();
    if (m_gbuffer_adjusted || (adjust_pipeline == nullptr) || !m_position_texture) {
        return;
    }
    command_buffer.transition_texture_layout(*m_position_texture,        Image_layout::general);
    command_buffer.transition_texture_layout(*m_normal_texture,          Image_layout::general);
    command_buffer.transition_texture_layout(*m_smooth_position_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_adjust_layout.get());
        encoder.set_compute_pipeline(*adjust_pipeline);
        encoder.set_acceleration_structure(0u, tlas);
        bind_instance_records(encoder);
        encoder.set_storage_image(2u, *m_position_texture);
        encoder.set_storage_image(3u, *m_normal_texture);
        encoder.set_storage_image(4u, *m_smooth_position_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_position_texture->get_width())  + 7) / 8,
            (static_cast<std::uintptr_t>(m_position_texture->get_height()) + 7) / 8,
            1
        );
    }
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_position_texture,        Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_normal_texture,          Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_smooth_position_texture, Image_layout::shader_read_only_optimal);
    m_gbuffer_adjusted = true;
    // The smooth position is now folded into the position G-buffer and
    // nothing else reads it; release the page-sized fp32 target until the
    // next G-buffer re-raster (ensure_gbuffer_targets recreates it).
    // Destruction is deferred behind the in-flight frames, so the commands
    // recorded above stay valid.
    m_smooth_position_texture.reset();
}

void Lightmap_baker::set_baking_enabled(const bool enabled)
{
    if (enabled == m_baking_enabled) {
        return;
    }
    // Pause semantics: the working set (accumulation, sweeps, G-buffer,
    // BLAS/TLAS) is deliberately KEPT on disable so re-enabling continues
    // where it paused; request_reset() is the explicit restart and
    // release_working_set() the explicit memory release.
    m_baking_enabled = enabled;
    if (!enabled) {
        m_pause_after_sweep = false;
        queue_dirty_tiles_for_save();
    } else {
        // The disk streamer may have owned the lightmap binding while
        // paused and overwritten the primitives' uv mappings with ITS
        // atlas slots; re-publish ours on the first tick (sampling the
        // baker display atlas through streamer offsets renders garbage).
        m_regions_published = false;
        // A paused-scene staleness white-out ends here: the first tick's
        // own hash checks perform the real reset/relayout and rebake.
        m_scene_stale = false;
    }
}

void Lightmap_baker::queue_dirty_tiles_for_save()
{
    // Pause autosave: park every resident, published, unsaved tile in the
    // pending-save queue; the owner's drain (Lightmap_window::update)
    // persists them at its safe point. The disk set then matches the paused
    // display, so the streamer takes the lightmap binding over seamlessly
    // (same pixels) and camera-driven disk tile streaming keeps working
    // while paused. Gated on the save-on-evict owner flag - without a
    // drain owner the queue would only grow.
    if (!m_save_on_evict) {
        return;
    }
    for (std::size_t i = 0; (i < m_tiles.size()) && (i < m_layout.tiles.size()); ++i) {
        const Tile_state& state = m_tiles[i];
        const int tile = static_cast<int>(i);
        if (!state.published || !state.dirty_since_save) {
            continue;
        }
        if (m_layout.tiles[i].slot < 0) {
            continue; // the readback needs a display slot
        }
        if (std::find(m_tiles_pending_save.begin(), m_tiles_pending_save.end(), tile) == m_tiles_pending_save.end()) {
            m_tiles_pending_save.push_back(tile);
        }
    }
}

void Lightmap_baker::request_single_iteration()
{
    m_pause_after_sweep   = true;
    m_pause_target_sweeps = get_sweep_count() + 1;
    m_baking_enabled      = true;
}

auto Lightmap_baker::has_published_display() const -> bool
{
    if (!m_display_texture) {
        return false;
    }
    for (const Tile_state& tile : m_tiles) {
        if (tile.published) {
            return true;
        }
    }
    return false;
}

auto Lightmap_baker::has_unsaved_published_display() const -> bool
{
    if (!m_display_texture) {
        return false;
    }
    for (const Tile_state& tile : m_tiles) {
        if (tile.published && tile.dirty_since_save) {
            return true;
        }
    }
    return false;
}

void Lightmap_baker::clear_display_to_white()
{
    using namespace erhe::graphics;
    if (!m_display_texture) {
        // Not created yet: ensure_bake_targets / restore_tile create the
        // page already cleared white on first use.
        return;
    }
    // Standalone submit (same pattern as restore_tile): the fresh layout's
    // mappings must not sample the previous bake's texels at the previous
    // packing. White = the unbaked look (matches the piece white-fallback
    // sentinel); alpha 0 keeps "no coverage" for atlas readback consumers.
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.clear_texture(*m_display_texture, {1.0, 1.0, 1.0, 0.0});
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();
    m_display_cleared = true;
    m_slots_pending_white_clear.clear(); // the full-page clear covers them
    for (Tile_state& tile : m_tiles) {
        tile.published = false;
    }
    log_render->info("Lightmap_baker: display atlas cleared to white");
}

void Lightmap_baker::clear_tiles()
{
    // Forget all baked content (Clear All Tiles): white display, restart
    // accumulation, drop the pending save/restore queues. Filesystem
    // cleanup (Lightmap_tile_io::delete_tile_set) is the caller's job.
    clear_display_to_white();
    for (Tile_state& state : m_tiles) {
        state.accum_dirty         = true;
        state.sweeps              = 0;
        state.dirty_since_save    = false;
        state.restore_hold_sweeps = 0;
        // The disk set is being deleted; future restore attempts decline
        // quietly on the missing manifest.
        state.restore_attempted   = false;
    }
    m_tiles_pending_save.clear();
    m_tiles_pending_restore.clear();
    m_reset_requested = true;
    if (!m_baking_enabled) {
        // Paused: reuse the stale-display takeover so the editor binds the
        // (white) baker display instead of leaving a dropped streamer
        // texture in the binding. Start clears the flag and rebakes.
        publish_regions();
        m_scene_stale = true;
    }
    log_render->info("Lightmap_baker: all tiles cleared");
}

void Lightmap_baker::release_working_set()
{
    // Everything here is rebuilt on demand by the next enabled tick
    // (bake_gbuffer / ensure_bake_targets / collect_instances); only the
    // display atlas the forward renderer samples stays resident, so the
    // scene keeps its last published lighting. No automatic caller since
    // Stop became Pause (2026-08-05); kept for explicit memory release
    // (e.g. a future scene-close cleanup).
    // Texture/BLAS destruction is deferred behind the in-flight frames.
    m_position_texture.reset();
    m_normal_texture.reset();
    m_albedo_texture.reset();
    m_smooth_position_texture.reset();
    m_origin_texture.reset();
    m_origin_valid     = false;
    m_origin_factor    = 0;
    m_lightmap_texture.reset();
    m_dilate_texture.reset();
    m_gbuffer_valid    = false;
    m_gbuffer_tile     = -1;
    m_gbuffer_adjusted = false;
    m_cursor_tile      = 0;
    m_cursor_y         = 0;
    for (Tile_state& tile : m_tiles) {
        tile.accum.reset();
        tile.accum_dirty      = true;
        tile.sweeps           = 0;
        tile.dirty_since_save = false;
        // tile.published stays: the display atlas keeps its last publish.
    }
    m_blas_cache.clear();
    m_tlas.reset();
    m_tlas_capacity = 0;
    for (Tlas_slot& slot : m_tlas_slots) {
        slot.acceleration_structure.reset();
        slot.capacity = 0;
    }
    log_render->info("Lightmap_baker: bake working set released (display atlas kept)");
}

void Lightmap_baker::set_options(const Bake_options& options)
{
    if (options.terminator_fix != m_options.terminator_fix) {
        // The virtual-offset pass folds the smooth position into the
        // position G-buffer in place; switching variants needs fresh
        // positions.
        m_gbuffer_valid   = false;
        m_reset_requested = true;
    }
    if (options.indirect_bounce != m_options.indirect_bounce) {
        m_reset_requested = true; // accumulated samples already mix in bounce light
    }
    if (options.coverage_mode != m_options.coverage_mode) {
        // Coverage decides which texels the raster writes; re-raster.
        m_gbuffer_valid   = false;
        m_reset_requested = true;
    }
    if (options.supersample_factor != m_options.supersample_factor) {
        // The origin target is (re)built in bake_gbuffer; accumulated
        // samples already used the other origin strategy / density.
        m_gbuffer_valid   = false;
        m_reset_requested = true;
    }
    if ((options.denoise       != m_options.denoise      ) ||
        (options.dilation      != m_options.dilation     ) ||
        (options.seam_blend    != m_options.seam_blend   ) ||
        (options.gutter_texels != m_options.gutter_texels)) {
        m_publish_requested = true; // republish the current average with the new stages
    }
    m_options = options;
}

void Lightmap_baker::record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer, const int tile, const bool with_denoise)
{
    using namespace erhe::graphics;

    erhe::graphics::Texture* const accum =
        ((tile >= 0) && (tile < static_cast<int>(m_tiles.size()))) ? m_tiles[static_cast<std::size_t>(tile)].accum.get() : nullptr;
    if ((accum == nullptr) || !m_lightmap_texture) {
        return;
    }
    const int tile_size = m_layout.get_tile_size();

    // Resolve the tile's running average into the working atlas, optionally
    // JNLM denoise it, then dilate; never touches the accumulation buffer.
    // The dilation ping-pong iteration count is chosen so the final pass
    // lands back in m_lightmap_texture: without denoise it starts there
    // (even count), with denoise it starts in the scratch the denoiser
    // wrote (odd count).
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_resolve_layout.get());
        encoder.set_compute_pipeline(*m_resolve_pipeline);
        encoder.set_storage_image(0u, *accum);
        encoder.set_storage_image(1u, *m_lightmap_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
            (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
            1
        );
    }
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    bool denoised = false;
    if (with_denoise && m_denoise_pipeline && m_dilate_texture) {
        command_buffer.transition_texture_layout(*m_dilate_texture, Image_layout::general);
        command_buffer.transition_texture_layout(*m_normal_texture, Image_layout::general);
        command_buffer.transition_texture_layout(*m_albedo_texture, Image_layout::general);
        {
            Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
            encoder.set_bind_group_layout(m_denoise_layout.get());
            encoder.set_compute_pipeline(*m_denoise_pipeline);
            encoder.set_storage_image(0u, *m_lightmap_texture);
            encoder.set_storage_image(1u, *m_dilate_texture);
            encoder.set_storage_image(2u, *m_normal_texture);
            encoder.set_storage_image(3u, *m_albedo_texture);
            encoder.dispatch_compute(
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                1
            );
        }
        command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        command_buffer.transition_texture_layout(*m_normal_texture, Image_layout::shader_read_only_optimal);
        command_buffer.transition_texture_layout(*m_albedo_texture, Image_layout::shader_read_only_optimal);
        denoised = true;
    }
    if (m_options.dilation && m_dilate_pipeline && m_dilate_texture) {
        command_buffer.transition_texture_layout(*m_dilate_texture, Image_layout::general);
        // Gutter-aware clamp: each dilation iteration grows charts one
        // texel; more than half the chart gutter would fill texels the
        // neighboring chart's sampling footprint owns (opposing fronts
        // never overwrite each other, but the FILL itself is what filter
        // taps read - a chart edge sampled bicubically reaches 2 texels
        // out, so the gutter must both be wide enough AND stay half-owned).
        const int gutter_limit     = std::max(1, static_cast<int>(std::floor(0.5f * m_options.gutter_texels)));
        const int dilate_iterations = std::min(s_padding, gutter_limit);
        Texture* const ping[2] = {
            denoised ? m_dilate_texture.get()   : m_lightmap_texture.get(),
            denoised ? m_lightmap_texture.get() : m_dilate_texture.get()
        };
        for (int i = 0; i < dilate_iterations; ++i) {
            {
                Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_dilate_layout.get());
                encoder.set_compute_pipeline(*m_dilate_pipeline);
                encoder.set_storage_image(0u, *ping[i & 1]);
                encoder.set_storage_image(1u, *ping[(i + 1) & 1]);
                encoder.dispatch_compute(
                    (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                    (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
                    1
                );
            }
            command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        }
        // The iteration count is now driven by the gutter, not by ping-pong
        // parity; copy back when the final write landed in the scratch.
        if (ping[dilate_iterations & 1] != m_lightmap_texture.get()) {
            command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::transfer_src_optimal);
            command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_dst_optimal);
            {
                Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
                blit.copy_from_texture(m_dilate_texture.get(), m_lightmap_texture.get());
            }
        }
    } else if (denoised) {
        // Dilation is off but the denoiser wrote into the scratch; copy it
        // back so the published atlas holds the denoised result.
        command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::transfer_src_optimal);
        command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_dst_optimal);
        {
            Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(m_dilate_texture.get(), m_lightmap_texture.get());
        }
    }
    if (m_options.seam_blend) {
        record_seam_blend(command_buffer, tile);
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
}

void Lightmap_baker::record_display_publish(erhe::graphics::Command_buffer& command_buffer, const int tile)
{
    using namespace erhe::graphics;
    if (!m_lightmap_texture || !m_display_texture) {
        return;
    }
    // Publishing goes through the tile's display slot; non-resident tiles
    // have nowhere to publish (their regions render unlit anyway).
    const int slot = ((tile >= 0) && (tile < m_layout.get_tile_count()))
        ? m_layout.tiles[static_cast<std::size_t>(tile)].slot
        : -1;
    if (slot < 0) {
        return;
    }
    const int        tile_size   = m_layout.get_tile_size();
    const glm::ivec2 slot_origin = m_layout.get_slot_origin(slot);
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    command_buffer.transition_texture_layout(*m_display_texture,  Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_lightmap_texture.get(),
            0, 0,
            glm::ivec3{0, 0, 0},
            glm::ivec3{tile_size, tile_size, 1},
            m_display_texture.get(),
            0, 0,
            glm::ivec3{slot_origin.x, slot_origin.y, 0}
        );
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_display_texture,  Image_layout::shader_read_only_optimal);
}

void Lightmap_baker::record_seam_blend(erhe::graphics::Command_buffer& command_buffer, const int tile)
{
    using namespace erhe::graphics;
    if (!m_seam_pipeline || !m_dilate_texture) {
        return;
    }
    if ((tile < 0) || (tile >= static_cast<int>(m_tile_seam_ranges.size()))) {
        return;
    }
    const auto [first_vertex, vertex_count] = m_tile_seam_ranges[static_cast<std::size_t>(tile)];
    if (vertex_count == 0) {
        return;
    }
    const int tile_size = m_layout.get_tile_size();
    // Sample source: a copy of the working atlas in the dilate scratch
    // (rendering into the atlas while sampling it would be a hazard).
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(m_lightmap_texture.get(), m_dilate_texture.get());
    }
    command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::color_attachment_optimal);

    const std::size_t byte_count = static_cast<std::size_t>(vertex_count) * sizeof(Seam_vertex);
    Ring_buffer_range vertex_range = m_seam_vertex_ring->acquire(Ring_buffer_usage::CPU_write, byte_count);
    {
        std::span<std::byte> gpu_data = vertex_range.get_span();
        std::memcpy(gpu_data.data(), m_seam_vertices.data() + first_vertex, byte_count);
        vertex_range.bytes_written(byte_count);
        vertex_range.close();
    }

    Render_pass_descriptor descriptor{};
    Render_pass_attachment_descriptor& attachment = descriptor.color_attachments[0];
    attachment.texture       = m_lightmap_texture.get();
    attachment.load_action   = Load_action::Load;
    attachment.store_action  = Store_action::Store;
    attachment.usage_before  = Image_usage_flag_bit_mask::storage;
    attachment.layout_before = Image_layout::color_attachment_optimal;
    attachment.usage_after   = Image_usage_flag_bit_mask::storage;
    attachment.layout_after  = Image_layout::color_attachment_optimal;
    descriptor.render_target_width  = tile_size;
    descriptor.render_target_height = tile_size;
    descriptor.debug_label = erhe::utility::Debug_label{"lightmap seam blend"};
    {
        Render_pass            render_pass{m_graphics_device, descriptor};
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        const Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, tile_size, tile_size);
        encoder.set_scissor_rect (0, 0, tile_size, tile_size);
        encoder.set_bind_group_layout(m_seam_layout.get());
        encoder.set_render_pipeline(*m_seam_pipeline);
        encoder.set_sampled_image(0u, *m_dilate_texture, *m_linear_sampler);
        encoder.set_vertex_buffer(vertex_range.get_buffer()->get_buffer(), vertex_range.get_byte_start_offset_in_buffer(), 0);
        encoder.draw_primitives(Primitive_type::line, 0, vertex_count);
    }
    vertex_range.release();
}

auto Lightmap_baker::compute_region_transform_hash() const -> uint64_t
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (const Instance_region& region : m_layout.regions) {
        const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
        if (node != nullptr) {
            const glm::mat4 world_from_node = node->world_from_node();
            hash = fnv1a64(&world_from_node, sizeof(world_from_node), hash);
        }
    }
    return hash;
}

// Change detection (plan section 3a step 1), cheapest response first.
// Three tiers of FNV hashes over the bake inputs:
//   layout   - the lightmapped set + grid parameters -> redo atlas layout
//   gbuffer  - lightmapped region transforms         -> re-raster G-buffer
//   lighting - lights + occluder transforms          -> reset accumulation
// Each tier implies the tiers below it. Shared by the tick and the paused
// monitor (monitor_paused_scene).
auto Lightmap_baker::compute_scene_hashes(Scene_root& scene_root) const -> Scene_hashes
{
    Scene_hashes result;
    uint64_t hash_layout = 0xcbf29ce484222325ull;
    {
        // Grid parameters (cell size + quadtree overrides): a subdivide /
        // merge or cell-size change relayouts (and, with a live partition,
        // the re-prepare commit swaps the pieces, which re-triggers this
        // through their buffer-mesh pointers).
        const uint64_t grid_hash = get_grid_parameters_hash();
        hash_layout = fnv1a64(&grid_hash, sizeof(grid_hash), hash_layout);
        const Scene_root* const scene_ptr = &scene_root;
        hash_layout = fnv1a64(&scene_ptr, sizeof(scene_ptr), hash_layout);
        for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
            if (!mesh || mesh->skin) {
                continue;
            }
            if ((mesh->get_flag_bits() & erhe::Item_flags::lightmapped) == 0u) {
                continue;
            }
            const erhe::scene::Mesh* const mesh_ptr = mesh.get();
            hash_layout = fnv1a64(&mesh_ptr, sizeof(mesh_ptr), hash_layout);
            for (const erhe::scene::Mesh_primitive& mesh_primitive : mesh->get_primitives()) {
                const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                const erhe::primitive::Buffer_mesh* const buffer_mesh = (primitive != nullptr) ? primitive->get_renderable_mesh() : nullptr;
                hash_layout = fnv1a64(&buffer_mesh, sizeof(buffer_mesh), hash_layout);
            }
        }
        // World-space partition state: piece meshes are not lightmapped-
        // flagged (the loop above skips them), so prepare/revert and piece
        // swaps must invalidate the layout through their own hash
        // contribution.
        if ((m_partitioner != nullptr) && m_partitioner->is_prepared() && (m_partitioner->get_scene_root() == &scene_root)) {
            for (const Lightmap_partitioner::Original_entry& entry : m_partitioner->get_entries()) {
                const erhe::scene::Mesh* const piece_mesh_ptr = entry.piece_mesh.get();
                hash_layout = fnv1a64(&piece_mesh_ptr, sizeof(piece_mesh_ptr), hash_layout);
                if (entry.piece_mesh) {
                    for (const erhe::scene::Mesh_primitive& mesh_primitive : entry.piece_mesh->get_primitives()) {
                        const erhe::primitive::Primitive* const primitive = mesh_primitive.primitive.get();
                        const erhe::primitive::Buffer_mesh* const buffer_mesh = (primitive != nullptr) ? primitive->get_renderable_mesh() : nullptr;
                        hash_layout = fnv1a64(&buffer_mesh, sizeof(buffer_mesh), hash_layout);
                    }
                }
            }
        }
    }
    result.layout  = hash_layout;
    result.gbuffer = compute_region_transform_hash();
    uint64_t hash_lighting = 0xcbf29ce484222325ull;
    for (const std::shared_ptr<erhe::scene::Light>& light : scene_root.layers().light()->lights) {
        if (!light || !light->is_visible()) {
            continue;
        }
        const erhe::scene::Node* const node = light->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        hash_lighting = fnv1a64(&world_from_node,           sizeof(world_from_node), hash_lighting);
        const glm::vec3 color = light->intensity * light->get_effective_color();
        hash_lighting = fnv1a64(&color,                     sizeof(color),           hash_lighting);
        hash_lighting = fnv1a64(&light->type,               sizeof(light->type),     hash_lighting);
        hash_lighting = fnv1a64(&light->range,              sizeof(light->range),    hash_lighting);
        hash_lighting = fnv1a64(&light->inner_spot_angle,   sizeof(float),           hash_lighting);
        hash_lighting = fnv1a64(&light->outer_spot_angle,   sizeof(float),           hash_lighting);
    }
    // Sky lighting (set_sky_lighting, called before tick): sun moves, sky
    // toggles or parameter edits invalidate accumulated samples like any
    // light edit.
    {
        const uint32_t sky_enabled = m_sky.enabled ? 1u : 0u;
        hash_lighting = fnv1a64(&sky_enabled,                     sizeof(uint32_t),  hash_lighting);
        hash_lighting = fnv1a64(&m_sky.sun_direction_and_intensity, sizeof(glm::vec4), hash_lighting);
        hash_lighting = fnv1a64(&m_sky.sky_params,                  sizeof(glm::vec4), hash_lighting);
    }
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        // Occluder set: any visible content mesh shadows the bake, so its
        // motion invalidates accumulated visibility. proxy_hidden sources
        // are skipped to mirror collect_instances (their render proxies
        // occlude); their motion still invalidates - through the stale-
        // source auto-re-prepare, whose commit swaps the piece meshes.
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        if ((mesh->get_flag_bits() & erhe::Item_flags::proxy_hidden) != 0u) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        hash_lighting = fnv1a64(&world_from_node, sizeof(world_from_node), hash_lighting);
    }
    result.lighting = hash_lighting;
    return result;
}

void Lightmap_baker::monitor_paused_scene(Scene_root& scene_root)
{
    // Paused change detection: the tick's hash checks only run while
    // baking, so without this a paused lightmap keeps showing pre-edit
    // lighting until Start. On any change: white-out the display, block
    // disk restores (payloads are pre-edit too) and flag the staleness -
    // the editor then keeps the lightmap binding on the (white) baker
    // display instead of the streamer's equally stale disk set. The
    // stored hashes stay untouched, so the first tick after Start still
    // performs the real relayout/reset/rebake.
    if (m_baking_enabled || m_scene_stale || !m_hashes_initialized) {
        return;
    }
    if ((m_layout_scene_root != &scene_root) || (m_layout.width == 0) || m_tiles.empty()) {
        return;
    }
    if (!has_published_display()) {
        return; // nothing stale is showing
    }
    const Scene_hashes hashes = compute_scene_hashes(scene_root);
    if ((hashes.layout == m_hash_layout) && (hashes.gbuffer == m_hash_gbuffer) && (hashes.lighting == m_hash_lighting)) {
        return;
    }
    clear_display_to_white();
    // The streamer may have owned the binding (and the mappings) since the
    // pause autosave; point the regions back at the (white) baker display.
    publish_regions();
    for (Tile_state& state : m_tiles) {
        state.restore_attempted = true;
    }
    m_tiles_pending_restore.clear();
    m_scene_stale = true;
    log_render->info("Lightmap_baker: scene changed while paused - display cleared to white until the next bake");
}

void Lightmap_baker::tick(
    erhe::graphics::Command_buffer& command_buffer,
    Scene_root&                     scene_root,
    const float                     min_face_texels,
    const glm::vec3*                camera_position,
    const int                       max_active_tiles
)
{
    using namespace erhe::graphics;

    if (!m_gather_pipeline || !m_resolve_pipeline) {
        return;
    }

    const Scene_hashes scene_hashes = compute_scene_hashes(scene_root);
    uint64_t hash_layout   = scene_hashes.layout;
    uint64_t hash_gbuffer  = scene_hashes.gbuffer;
    uint64_t hash_lighting = scene_hashes.lighting;

    bool reset = m_reset_requested;
    if (!m_hashes_initialized) {
        // First tick observes the current state without invalidating work
        // that standalone bakes (window / MCP) already produced.
        m_hashes_initialized = true;
        m_hash_layout        = hash_layout;
        m_hash_gbuffer       = hash_gbuffer;
        m_hash_lighting      = hash_lighting;
    } else {
        if ((hash_layout != m_hash_layout) || (m_layout_scene_root != &scene_root)) {
            m_hash_layout = hash_layout;
            if (!update_layout(scene_root, min_face_texels)) {
                return;
            }
            // Regions changed; re-derive their transform hash so the next
            // tick does not see a spurious G-buffer invalidation.
            hash_gbuffer = compute_region_transform_hash();
            reset = true;
            // The swap that changed the layout was applied THIS frame; its
            // vertex uploads have not been submitted yet (see the member's
            // comment). Bake the G-buffer next tick, not now.
            m_gbuffer_upload_defer = true;
        }
        if (hash_gbuffer != m_hash_gbuffer) {
            m_hash_gbuffer  = hash_gbuffer;
            m_gbuffer_valid = false;
            reset           = true;
        }
        if (hash_lighting != m_hash_lighting) {
            m_hash_lighting = hash_lighting;
            reset           = true;
        }
    }
    if (m_layout.width == 0) {
        if (!update_layout(scene_root, min_face_texels)) {
            return;
        }
        m_gbuffer_upload_defer = true;
    }
    if (m_tiles.empty()) {
        return;
    }

    // Residency (camera streaming): rank tiles by the distance from the
    // camera to their world bounds. The N nearest hold a display slot
    // (N = the slot grid) - RESIDENT tiles keep showing their published
    // lightmap. Of those, the max_active_tiles nearest (> 0; otherwise
    // all) actually GATHER - inactive-but-resident tiles keep their slot
    // and last publish but stop accumulating and release their fp32
    // accumulation. Tiles that lose their slot render unlit until the
    // camera returns. Without a camera the current residency stands.
    {
        const int tile_count   = static_cast<int>(m_tiles.size());
        const int slot_count   = m_layout.get_slot_count();
        const int max_resident = std::min(slot_count, tile_count);
        int       max_active   = max_resident;
        if (max_active_tiles > 0) {
            max_active = std::min(max_active, max_active_tiles);
        }
        if ((camera_position != nullptr) && (tile_count > 0)) {
            std::vector<int> order;
            order.reserve(static_cast<std::size_t>(tile_count));
            for (int tile = 0; tile < tile_count; ++tile) {
                if (m_tiles[static_cast<std::size_t>(tile)].has_content) {
                    order.push_back(tile);
                }
            }
            // XZ-plane distance (matches the spatial partition axes and the
            // disk streamer's ranking): camera height must not demote the
            // tile directly underneath in favor of a horizontally farther
            // but taller one.
            const auto distance_of = [this, camera_position](const int tile) -> float {
                const erhe::math::Aabb& bounds = m_layout.tiles[static_cast<std::size_t>(tile)].world_bounds;
                const glm::vec2 p{camera_position->x, camera_position->z};
                const glm::vec2 lo{bounds.min.x, bounds.min.z};
                const glm::vec2 hi{bounds.max.x, bounds.max.z};
                const glm::vec2 clamped = glm::clamp(p, lo, hi);
                return glm::distance(p, clamped);
            };
            std::sort(
                order.begin(),
                order.end(),
                [&](const int lhs, const int rhs) {
                    const float dl = distance_of(lhs);
                    const float dr = distance_of(rhs);
                    if (dl != dr) {
                        return dl < dr;
                    }
                    // Tie-break toward the currently resident tile so equal
                    // distances never thrash slots frame to frame.
                    const bool rl = m_layout.tiles[static_cast<std::size_t>(lhs)].slot >= 0;
                    const bool rr = m_layout.tiles[static_cast<std::size_t>(rhs)].slot >= 0;
                    if (rl != rr) {
                        return rl;
                    }
                    return lhs < rhs;
                }
            );
            // Two nested camera-ranked sets: want_active is a subset of
            // want_resident by construction (same order, smaller count).
            std::vector<char> want_resident(static_cast<std::size_t>(tile_count), 0);
            std::vector<char> want_active  (static_cast<std::size_t>(tile_count), 0);
            for (std::size_t i = 0; (i < order.size()) && (i < static_cast<std::size_t>(max_resident)); ++i) {
                want_resident[static_cast<std::size_t>(order[i])] = 1;
                if (i < static_cast<std::size_t>(max_active)) {
                    want_active[static_cast<std::size_t>(order[i])] = 1;
                }
            }
            // Free the slots of tiles leaving the resident set...
            bool residency_changed = false;
            std::vector<int> free_slots;
            for (int tile = 0; tile < tile_count; ++tile) {
                Tile& layout_tile = m_layout.tiles[static_cast<std::size_t>(tile)];
                Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
                const bool resident = want_resident[static_cast<std::size_t>(tile)] != 0;
                const bool active   = want_active  [static_cast<std::size_t>(tile)] != 0;
                if (!resident && (layout_tile.slot >= 0)) {
                    if (m_save_on_evict && state.published && state.dirty_since_save) {
                        // Unsaved bake results must reach disk before the
                        // slot is dropped: park the tile in the pending-save
                        // queue and keep its slot (gathering stops via
                        // active = false, so the content is stable for the
                        // readback). Lightmap_window::update() saves it and
                        // calls mark_tile_saved(); the next tick then
                        // completes the eviction.
                        if (std::find(m_tiles_pending_save.begin(), m_tiles_pending_save.end(), tile) == m_tiles_pending_save.end()) {
                            m_tiles_pending_save.push_back(tile);
                        }
                        state.active = false;
                        continue;
                    }
                    free_slots.push_back(layout_tile.slot);
                    layout_tile.slot = -1;
                    residency_changed = true;
                    state.published = false;
                    if (state.accum) {
                        // Destruction is deferred behind the in-flight frames.
                        state.accum.reset();
                    }
                    state.accum_dirty         = true;
                    state.sweeps              = 0;
                    state.dirty_since_save    = false;
                    state.restore_hold_sweeps = 0;
                    // The evicted content was just saved (or was already on
                    // disk); the next activation may restore it.
                    state.restore_attempted   = false;
                }
                state.active = active;
            }
            // ...and hand them to the tiles entering it.
            for (int tile = 0; tile < tile_count; ++tile) {
                Tile& layout_tile = m_layout.tiles[static_cast<std::size_t>(tile)];
                Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
                if ((want_resident[static_cast<std::size_t>(tile)] != 0) && (layout_tile.slot < 0) && !free_slots.empty()) {
                    layout_tile.slot = free_slots.back();
                    free_slots.pop_back();
                    residency_changed = true;
                    // Fresh occupant: accumulate and publish from scratch.
                    // The slot still holds the PREVIOUS occupant's texels
                    // and this tile's regions map into it right away
                    // (publish_regions below) - queue a white overwrite so
                    // the interim shows the unbaked look, not stale
                    // lighting (record_pending_slot_white_clears; a disk
                    // restore replaces the white and unqueues the slot).
                    m_slots_pending_white_clear.push_back(layout_tile.slot);
                    state.published           = false;
                    state.accum_dirty         = true;
                    state.sweeps              = 0;
                    state.dirty_since_save    = false;
                    state.restore_hold_sweeps = 0;
                }
            }
            if (residency_changed) {
                m_regions_published = false;
            }
        }
    }

    // Restore-on-activate: a resident tile with no accumulated content yet
    // (fresh occupant or initial residency) gets one shot at coming back
    // from disk instead of visibly re-baking from black. The owner drains
    // take_tile_pending_restore(), validates the payload, and calls
    // restore_tile() - which creates the display atlas itself when this is
    // the very first activity after a layout. Queued tiles do not gather
    // (tile_gathers below), so the drain always wins the race against the
    // first whole-tile sweep; an invalidation this tick clears the queue
    // again (the disk content just went stale).
    if (m_save_on_evict) {
        for (int tile = 0; tile < static_cast<int>(m_tiles.size()); ++tile) {
            Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
            // Residency (the slot check below), not activity, gates the
            // restore: an inactive-but-resident tile displays its slot
            // without gathering, so it needs its disk content too.
            if (!state.has_content || state.published || (state.sweeps != 0) || state.restore_attempted) {
                continue;
            }
            if (m_layout.tiles[static_cast<std::size_t>(tile)].slot < 0) {
                continue;
            }
            state.restore_attempted = true;
            m_tiles_pending_restore.push_back(tile);
        }
    }

    // Advance the cursor to a tile that actually gathers (has regions and
    // is within the camera clamp); bail if none is left. A tile queued for
    // a disk restore does not gather until the owner drains the queue
    // (next frame at the latest): small tiles complete a whole sweep per
    // tick, which would mark the tile published before the restore ever
    // ran and the attempt would decline.
    const int tile_count = static_cast<int>(m_tiles.size());
    const auto tile_gathers = [this](const int tile) -> bool {
        const Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
        return
            state.has_content &&
            state.active &&
            (std::find(m_tiles_pending_restore.begin(), m_tiles_pending_restore.end(), tile) == m_tiles_pending_restore.end());
    };
    if ((m_cursor_tile < 0) || (m_cursor_tile >= tile_count) || !tile_gathers(m_cursor_tile)) {
        int next = -1;
        for (int i = 0; i < tile_count; ++i) {
            const int candidate = (m_cursor_tile + i) % tile_count;
            if (tile_gathers(candidate)) {
                next = candidate;
                break;
            }
        }
        if (next < 0) {
            return;
        }
        m_cursor_tile = next;
        m_cursor_y    = 0;
    }

    if (reset) {
        // Light / occluder / transform edits invalidate every tile's
        // accumulation; the clears happen lazily as each tile becomes
        // current (ensure_tile_accum). The last publish is obsolete too, so
        // an eviction must not persist it.
        for (Tile_state& state : m_tiles) {
            state.accum_dirty         = true;
            state.sweeps              = 0;
            state.dirty_since_save    = false;
            state.restore_hold_sweeps = 0;
            // The saved payloads describe the pre-edit lighting; restoring
            // them would resurrect stale content.
            state.restore_attempted   = true;
        }
        m_tiles_pending_restore.clear();
        m_cursor_y = 0;
        // ACTIVE tiles keep showing their obsolete publish only until the
        // rebake replaces it (anti-blackout). Resident tiles OUTSIDE the
        // active clamp never rebake until the camera activates them - their
        // stale publish would show indefinitely; overwrite their slots with
        // white (the unbaked look) instead.
        std::size_t stale_inactive_slots = 0;
        for (int tile = 0; tile < static_cast<int>(m_tiles.size()); ++tile) {
            Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
            const int slot = m_layout.tiles[static_cast<std::size_t>(tile)].slot;
            if ((slot < 0) || state.active || !state.published) {
                continue;
            }
            state.published = false;
            ++stale_inactive_slots;
            if (std::find(m_slots_pending_white_clear.begin(), m_slots_pending_white_clear.end(), slot) == m_slots_pending_white_clear.end()) {
                m_slots_pending_white_clear.push_back(slot);
            }
        }
        if (stale_inactive_slots > 0) {
            log_render->info("Lightmap_baker: reset - {} inactive resident slots cleared to white (stale publish)", stale_inactive_slots);
        }
        // A reset mid-single-iteration re-arms the pause target against the
        // fresh (zeroed) sweep counts.
        if (m_pause_after_sweep) {
            m_pause_target_sweeps = 1;
        }
    }
    m_reset_requested = false;

    if (!m_gbuffer_valid || (m_gbuffer_tile != m_cursor_tile)) {
        // A layout change this tick means the swapped meshes' vertex uploads
        // are still queued in the frame command buffer, which is submitted
        // AFTER this standalone bake would run - the raster would read
        // not-yet-copied buffers and the affected regions would stay black
        // forever (the hashes match afterwards, so nothing re-rasters).
        // Skip one tick; the next tick runs after the upload-carrying frame
        // was submitted ahead of us on the same queue.
        if (m_gbuffer_upload_defer) {
            m_gbuffer_upload_defer = false;
            return;
        }
        // Standalone submit (wait_idle): a hitch on invalidation events
        // (transform edit of a lightmapped mesh) and on tile transitions
        // (once per tile per sweep on multi-tile pages).
        if (!bake_gbuffer(m_cursor_tile)) {
            return;
        }
        m_hash_gbuffer = compute_region_transform_hash();
    }

    ensure_bake_targets(command_buffer);
    record_pending_slot_white_clears(command_buffer);
    Texture* const accum = ensure_tile_accum(command_buffer, m_cursor_tile);
    if (accum == nullptr) {
        return;
    }
    Tile_state& cursor_state = m_tiles[static_cast<std::size_t>(m_cursor_tile)];

    const std::vector<Light_record> lights = collect_lights(scene_root);
    collect_instances(command_buffer, scene_root, m_tick_instances, m_tick_records);
    if (m_tick_instances.empty()) {
        return;
    }

    // Per-frame-in-flight TLAS slot (occluders may move every frame).
    const std::size_t slot_index = static_cast<std::size_t>(m_graphics_device.get_frame_index() % s_tlas_slot_count);
    Tlas_slot& slot = m_tlas_slots[slot_index];
    const uint32_t required_capacity = static_cast<uint32_t>(m_tick_instances.size());
    if (!slot.acceleration_structure || (slot.capacity < required_capacity)) {
        const uint32_t new_capacity = std::max(64u, std::bit_ceil(required_capacity));
        slot.acceleration_structure = std::make_unique<Acceleration_structure>(
            m_graphics_device,
            Acceleration_structure_create_info{
                .type               = Acceleration_structure_type::top_level,
                .max_instance_count = new_capacity,
                .debug_label        = erhe::utility::Debug_label{"Lightmap tick TLAS"}
            }
        );
        slot.capacity = new_capacity;
    }
    slot.acceleration_structure->build(command_buffer, m_tick_instances);

    // Instance records before the adjust pass: its committed_face_normal
    // position-fetch fallback reads them (the gather encoder below binds
    // the same range again).
    const std::size_t record_byte_count = m_tick_records.size() * sizeof(Lm_instance_record);
    Ring_buffer_range ssbo_range = m_tick_instance_ssbo->acquire(Ring_buffer_usage::CPU_write, record_byte_count);
    {
        std::span<std::byte> gpu_data = ssbo_range.get_span();
        std::memcpy(gpu_data.data(), m_tick_records.data(), record_byte_count);
        ssbo_range.bytes_written(record_byte_count);
        ssbo_range.close();
    }

    // Virtual offset (one-shot per G-buffer): needs a built TLAS.
    record_adjust(
        command_buffer,
        *slot.acceleration_structure,
        [&](Compute_command_encoder& encoder) {
            m_tick_instance_ssbo->bind(encoder, ssbo_range);
        }
    );

    // Budgeted band (plan section 3a step 2): the cursor walks the current
    // TILE top to bottom; small tiles sweep whole-tile every frame.
    const int tile_size   = m_layout.get_tile_size();
    const int rows_budget = std::clamp(c_texels_per_tick / std::max(tile_size, 1), 8, tile_size);
    const int band_rows   = std::min(rows_budget, tile_size - m_cursor_y);
    const uint32_t base_y = static_cast<uint32_t>(m_cursor_y);

    Ring_buffer_range ubo_range = m_tick_gather_ubo->acquire(Ring_buffer_usage::CPU_write, m_gather_block_size);
    {
        std::span<std::byte> gpu_data = ubo_range.get_span();
        write_gather_ubo(gpu_data.data(), lights, m_frame_counter, base_y);
        ubo_range.bytes_written(m_gather_block_size);
        ubo_range.close();
    }

    // Bounce rays sample the display atlas (instance records carry the
    // display slot mapping); one publish of feedback latency. Hits on
    // non-resident tiles contribute nothing (their mapping is zero).
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
    {
        Compute_pipeline* const ss_pipeline =
            (m_origin_factor == 8) ? m_gather_ss8_pipeline.get() :
            (m_origin_factor == 4) ? m_gather_ss4_pipeline.get() : nullptr;
        const bool supersample = m_origin_valid && m_origin_texture && (ss_pipeline != nullptr);
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_gather_layout.get());
        encoder.set_compute_pipeline(supersample ? *ss_pipeline : *m_gather_pipeline);
        m_tick_gather_ubo->bind(encoder, ubo_range);
        m_tick_instance_ssbo->bind(encoder, ssbo_range);
        encoder.set_acceleration_structure(2u, *slot.acceleration_structure);
        encoder.set_sampled_image(3u, *m_position_texture, *m_nearest_sampler);
        encoder.set_sampled_image(4u, *m_normal_texture,   *m_nearest_sampler);
        encoder.set_sampled_image(5u, *m_albedo_texture,   *m_linear_sampler);
        encoder.set_sampled_image(6u, *m_display_texture,  *m_linear_sampler);
        encoder.set_sampled_image(7u, (m_sky.transmittance_lut != nullptr) ? *m_sky.transmittance_lut : *m_albedo_texture, *m_linear_sampler);
        encoder.set_sampled_image(8u, (m_sky.multiscatter_lut  != nullptr) ? *m_sky.multiscatter_lut  : *m_albedo_texture, *m_linear_sampler);
        encoder.set_sampled_image(9u, supersample ? *m_origin_texture : *m_position_texture, *m_nearest_sampler);
        encoder.set_storage_image(12u, *accum);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(tile_size) + 7) / 8,
            (static_cast<std::uintptr_t>(band_rows) + 7) / 8,
            1
        );
    }
    ubo_range.release();
    ssbo_range.release();

    m_cursor_y += band_rows;
    const bool tile_sweep_completed = m_cursor_y >= tile_size;
    if (tile_sweep_completed) {
        m_cursor_y = 0;
        ++cursor_state.sweeps;
    }
    // Publish cadence (phase 4 denoise): while the tile has never been
    // published publish the raw average every tick so the lightmap appears
    // immediately; afterwards publish only on tile-sweep completion, with
    // JNLM denoise folded in. Steady-state mid-sweep ticks skip
    // resolve+dilate entirely - the viewport (and the bounce feedback)
    // keep sampling the last publish. A disk-restored tile additionally
    // holds republish until fresh accumulation reaches the restored sweep
    // count (restore_hold_sweeps) - replacing an N-sweep saved result with
    // a 1-sweep fresh one would visibly regress. dirty_since_save tracks
    // actual publishes: while the hold is in effect the display still
    // matches the disk payload, so an eviction need not re-save it.
    if (!cursor_state.published) {
        record_resolve_and_dilate(command_buffer, m_cursor_tile, false);
        record_display_publish(command_buffer, m_cursor_tile);
        if (tile_sweep_completed) {
            cursor_state.published        = true;
            cursor_state.dirty_since_save = true;
        }
    } else if ((tile_sweep_completed || m_publish_requested) && (cursor_state.sweeps >= cursor_state.restore_hold_sweeps)) {
        // The hold also beats an explicit republish request (set_options
        // stage toggles): resolving a 1-sweep accumulation over restored
        // N-sweep content is exactly the regression the hold prevents.
        record_resolve_and_dilate(command_buffer, m_cursor_tile, m_options.denoise);
        record_display_publish(command_buffer, m_cursor_tile);
        cursor_state.published           = true;
        cursor_state.dirty_since_save    = true;
        cursor_state.restore_hold_sweeps = 0;
    }
    m_publish_requested = false;
    m_lightmap_valid = true;
    if (!m_regions_published) {
        publish_regions();
    }
    // Tile rotation: after finishing a sweep of this tile move to the next
    // gathering tile (wraps; single-tile pages keep sweeping in place).
    if (tile_sweep_completed) {
        for (int i = 1; i <= tile_count; ++i) {
            const int candidate = (m_cursor_tile + i) % tile_count;
            if (tile_gathers(candidate)) {
                m_cursor_tile = candidate;
                break;
            }
        }
    }
    ++m_frame_counter;

    // Single Iteration: pause once the minimum active-tile sweep count
    // reaches the requested target (every active content tile completed at
    // least one more full sweep). Goes through set_baking_enabled so the
    // pause autosave queues the finished tiles.
    if (m_pause_after_sweep && (get_sweep_count() >= m_pause_target_sweeps)) {
        set_baking_enabled(false);
        log_render->info("Lightmap_baker: single iteration complete at sweep {} - paused", get_sweep_count());
    }
}

auto Lightmap_baker::take_tile_pending_save() -> int
{
    if (m_tiles_pending_save.empty()) {
        return -1;
    }
    const int tile = m_tiles_pending_save.back();
    m_tiles_pending_save.pop_back();
    return tile;
}

void Lightmap_baker::mark_tile_saved(const int tile)
{
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size()))) {
        return;
    }
    m_tiles[static_cast<std::size_t>(tile)].dirty_since_save = false;
}

auto Lightmap_baker::take_tile_pending_restore() -> int
{
    if (m_tiles_pending_restore.empty()) {
        return -1;
    }
    const int tile = m_tiles_pending_restore.back();
    m_tiles_pending_restore.pop_back();
    return tile;
}

auto Lightmap_baker::get_tile_sweeps(const int tile) const -> uint32_t
{
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size()))) {
        return 0;
    }
    return m_tiles[static_cast<std::size_t>(tile)].sweeps;
}

auto Lightmap_baker::is_tile_active(const int tile) const -> bool
{
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size()))) {
        return false;
    }
    const Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
    return state.has_content && state.active;
}

auto Lightmap_baker::restore_tile(
    const int                       tile,
    const int                       width,
    const int                       height,
    const std::span<const uint16_t> rgba16,
    const uint32_t                  saved_sweeps
) -> bool
{
    using namespace erhe::graphics;
    if ((tile < 0) || (tile >= m_layout.get_tile_count()) || (tile >= static_cast<int>(m_tiles.size())) || (m_layout.width == 0)) {
        return false;
    }
    const int tile_size = m_layout.get_tile_size();
    if ((width != tile_size) || (height != tile_size)) {
        return false;
    }
    const std::size_t expected = static_cast<std::size_t>(tile_size) * static_cast<std::size_t>(tile_size) * 4u;
    if (rgba16.size() != expected) {
        return false;
    }
    const int slot = m_layout.tiles[static_cast<std::size_t>(tile)].slot;
    Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
    if ((slot < 0) || state.published || (state.sweeps != 0)) {
        return false; // evicted or already progressed since it was queued
    }

    // The inverse of read_back_tile: staging buffer -> the tile's display
    // slot sub-rect, standalone submit.
    const std::size_t bytes_per_row = static_cast<std::size_t>(tile_size) * 8u; // RGBA16F
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(tile_size);
    Buffer staging{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_src,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent,
            .debug_label = erhe::utility::Debug_label{"lightmap tile restore staging"}
        }
    };
    {
        const std::span<std::byte> mapped = staging.map_bytes(0, byte_count);
        std::memcpy(mapped.data(), rgba16.data(), byte_count);
        staging.unmap();
    }
    // The very first activity after a layout runs before any tick created
    // the display atlas (ensure_bake_targets); create and clear it here so
    // the restore does not have to lose that race - and so the deferred
    // clear does not wipe the restored slot afterwards.
    const bool page_matches =
        m_display_texture &&
        (m_display_texture->get_width()  == m_layout.width) &&
        (m_display_texture->get_height() == m_layout.height);
    if (!page_matches) {
        m_display_texture = std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    Image_usage_flag_bit_mask::sampled      |
                    Image_usage_flag_bit_mask::transfer_src |
                    Image_usage_flag_bit_mask::transfer_dst,
                .type        = Texture_type::texture_2d,
                .pixelformat = c_atlas_format,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{"lightmap display atlas"}
            }
        );
        m_display_cleared = false;
    }

    const glm::ivec2 slot_origin = m_layout.get_slot_origin(slot);
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    if (!m_display_cleared) {
        // Same semantics as the ensure_bake_targets clear (which this
        // preempts): fresh page, no tile holds a publish yet. White = the
        // unbaked look (matches the piece white-fallback sentinel); alpha 0
        // keeps "no coverage" for the atlas readback consumers.
        command_buffer.clear_texture(*m_display_texture, {1.0, 1.0, 1.0, 0.0});
        command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
        m_display_cleared = true;
        for (Tile_state& tile_state : m_tiles) {
            tile_state.published = false;
        }
    }
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_buffer(
            &staging,
            0,
            bytes_per_row,
            byte_count,
            glm::ivec3{tile_size, tile_size, 1},
            m_display_texture.get(),
            0, 0,
            glm::ivec3{slot_origin.x, slot_origin.y, 0}
        );
    }
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    // The restored pixels replace the fresh-slot white overwrite; a still-
    // queued clear for this slot would wipe them on the next tick.
    std::erase(m_slots_pending_white_clear, slot);
    // Published disk content: the display matches the payload, so it is not
    // dirty; accumulation still restarts (accum_dirty stands), but republish
    // waits until it has re-earned the restored quality.
    state.published           = true;
    state.dirty_since_save    = false;
    state.restore_hold_sweeps = std::max(saved_sweeps, 1u);
    m_lightmap_valid          = true;
    log_render->info("Lightmap_baker: tile {} restored from disk ({} sweeps)", tile, saved_sweeps);
    return true;
}

auto Lightmap_baker::tile_has_unsaved_content(const int tile) const -> bool
{
    if ((tile < 0) || (tile >= static_cast<int>(m_tiles.size())) || (tile >= m_layout.get_tile_count())) {
        return false;
    }
    const Tile_state& state = m_tiles[static_cast<std::size_t>(tile)];
    return
        (m_layout.tiles[static_cast<std::size_t>(tile)].slot >= 0) &&
        state.published &&
        state.dirty_since_save;
}

auto Lightmap_baker::read_back_tile(const int tile, std::vector<uint16_t>& out_rgba16) -> bool
{
    using namespace erhe::graphics;
    if ((tile < 0) || (tile >= m_layout.get_tile_count()) || !m_display_texture) {
        return false;
    }
    if (m_display_texture->get_pixelformat() != erhe::dataformat::Format::format_16_vec4_float) {
        return false;
    }
    const int slot = m_layout.tiles[static_cast<std::size_t>(tile)].slot;
    if ((slot < 0) || !m_tiles[static_cast<std::size_t>(tile)].published) {
        return false;
    }
    // Region readback of the tile's display slot: the display atlas is
    // already fp16, so the payload bytes come out directly.
    const int         tile_size     = m_layout.get_tile_size();
    const glm::ivec2  slot_origin   = m_layout.get_slot_origin(slot);
    const std::size_t bytes_per_row = static_cast<std::size_t>(tile_size) * 8u; // RGBA16F
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(tile_size);
    Buffer readback{
        m_graphics_device,
        Buffer_create_info{
            .capacity_byte_count                    = byte_count,
            .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
            .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
            .required_memory_property_bit_mask      =
                Memory_property_flag_bit_mask::host_read |
                Memory_property_flag_bit_mask::host_write,
            .preferred_memory_property_bit_mask     =
                Memory_property_flag_bit_mask::host_coherent |
                Memory_property_flag_bit_mask::host_persistent,
            .debug_label = erhe::utility::Debug_label{"lightmap tile save readback"}
        }
    };
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_display_texture.get(),
            0, 0,
            glm::ivec3{slot_origin.x, slot_origin.y, 0},
            glm::ivec3{tile_size, tile_size, 1},
            &readback,
            0,
            bytes_per_row,
            byte_count
        );
    }
    command_buffer.transition_texture_layout(*m_display_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
    out_rgba16.resize(static_cast<std::size_t>(tile_size) * static_cast<std::size_t>(tile_size) * 4u);
    std::memcpy(out_rgba16.data(), mapped.data(), byte_count);
    readback.unmap();
    return true;
}

auto Lightmap_baker::read_lightmap(std::vector<float>& out_rgba) -> bool
{
    using namespace erhe::graphics;
    // Read the display atlas: that is the published result the renderer
    // samples (the working atlas may hold a partial post-reset sweep).
    erhe::graphics::Texture* const source = m_display_texture.get();
    if (!m_lightmap_valid || (source == nullptr)) {
        return false;
    }
    return read_rgba_texture_to_float(m_graphics_device, *source, out_rgba);
}

// Per-facet chart order keys (leak camouflage; see build_chart_order_keys
// in the header): mean baked luminance per facet, averaged over the covered
// texels of the facet's UV bounding box from a CPU readback of the
// published atlas (centroid texel as fallback for facets with no coverage).
auto Lightmap_baker::build_chart_order_keys(const int tile) -> std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>
{
    std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>> keys;
    std::vector<float> atlas;
    if (!read_lightmap(atlas) || (m_layout.width == 0)) {
        return keys;
    }
    const int page_width  = m_layout.width;
    const int page_height = m_layout.height;
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh) {
            continue;
        }
        if ((tile >= 0) && (region.tile != tile)) {
            continue;
        }
        const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
        if (region.primitive_index >= primitives.size()) {
            continue;
        }
        const erhe::primitive::Primitive* const primitive = primitives[region.primitive_index].primitive.get();
        if ((primitive == nullptr) || !primitive->render_shape) {
            continue;
        }
        const std::shared_ptr<erhe::geometry::Geometry>& geometry = primitive->render_shape->get_geometry();
        if (!geometry) {
            continue;
        }
        erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
        if (!attributes.corner_texcoord_2.has(0)) {
            continue;
        }
        // The readback is the display atlas: only resident (slot-holding)
        // regions have baked texels to sample.
        const glm::vec4 display_scale_offset = m_layout.display_uv_scale_offset(region);
        if (display_scale_offset.x <= 0.0f) {
            continue;
        }
        const GEO::Mesh& geo_mesh = geometry->get_mesh();
        std::vector<float>& facet_keys = keys[geometry.get()];
        facet_keys.assign(geo_mesh.facets.nb(), 0.0f);
        for (GEO::index_t facet : geo_mesh.facets) {
            const GEO::index_t corner_count = geo_mesh.facets.nb_corners(facet);
            if (corner_count == 0) {
                continue;
            }
            GEO::vec2f uv_sum{0.0f, 0.0f};
            GEO::vec2f uv_min{ std::numeric_limits<float>::max(),  std::numeric_limits<float>::max()};
            GEO::vec2f uv_max{-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};
            GEO::index_t uv_count = 0;
            for (GEO::index_t corner : geo_mesh.facets.corners(facet)) {
                const std::optional<GEO::vec2f> uv = attributes.corner_texcoord_2.try_get(corner);
                if (uv.has_value()) {
                    uv_sum = uv_sum + uv.value();
                    uv_min.x = std::min(uv_min.x, uv.value().x);
                    uv_min.y = std::min(uv_min.y, uv.value().y);
                    uv_max.x = std::max(uv_max.x, uv.value().x);
                    uv_max.y = std::max(uv_max.y, uv.value().y);
                    ++uv_count;
                }
            }
            if (uv_count == 0) {
                continue;
            }
            const auto texel_x_of = [&display_scale_offset, page_width](const float u) -> int {
                const float atlas_u = u * display_scale_offset.x + display_scale_offset.z;
                return std::clamp(static_cast<int>(atlas_u * static_cast<float>(page_width)), 0, page_width - 1);
            };
            const auto texel_y_of = [&display_scale_offset, page_height](const float v) -> int {
                const float atlas_v = v * display_scale_offset.y + display_scale_offset.w;
                return std::clamp(static_cast<int>(atlas_v * static_cast<float>(page_height)), 0, page_height - 1);
            };
            const auto luminance_at = [&atlas, page_width](const int texel_x, const int texel_y) -> float {
                const std::size_t index = (static_cast<std::size_t>(texel_y) * page_width + texel_x) * 4u;
                return
                    0.2126f * atlas[index + 0] +
                    0.7152f * atlas[index + 1] +
                    0.0722f * atlas[index + 2];
            };
            // Average all covered texels (alpha > 0 = rasterized chart
            // interior) in the facet's UV bounding box; a single centroid
            // texel is too noisy for tiny per-facet charts - it can land on
            // a gutter or dilated texel and interleave dark/bright facets
            // in the pack sort. Strided so huge facets stay bounded.
            int x0 = texel_x_of(uv_min.x);
            int x1 = texel_x_of(uv_max.x);
            int y0 = texel_y_of(uv_min.y);
            int y1 = texel_y_of(uv_max.y);
            if (x1 < x0) { std::swap(x0, x1); }
            if (y1 < y0) { std::swap(y0, y1); }
            const int stride_x = std::max(1, (x1 - x0 + 1) / 64);
            const int stride_y = std::max(1, (y1 - y0 + 1) / 64);
            double      luminance_sum = 0.0;
            std::size_t covered_count = 0;
            for (int texel_y = y0; texel_y <= y1; texel_y += stride_y) {
                for (int texel_x = x0; texel_x <= x1; texel_x += stride_x) {
                    const std::size_t index = (static_cast<std::size_t>(texel_y) * page_width + texel_x) * 4u;
                    if (atlas[index + 3] <= 0.0f) {
                        continue;
                    }
                    // The bake can leave non-finite texels (NaN irradiance
                    // from degenerate bounce math); one would poison the
                    // whole average - and a NaN key breaks the packer's
                    // sort comparator (strict weak ordering).
                    const float luminance = luminance_at(texel_x, texel_y);
                    if (!std::isfinite(luminance)) {
                        continue;
                    }
                    luminance_sum += luminance;
                    ++covered_count;
                }
            }
            if (covered_count > 0) {
                facet_keys[facet] = static_cast<float>(luminance_sum / static_cast<double>(covered_count));
            } else {
                const GEO::vec2f centroid = uv_sum / static_cast<float>(uv_count);
                const float luminance = luminance_at(texel_x_of(centroid.x), texel_y_of(centroid.y));
                facet_keys[facet] = std::isfinite(luminance) ? luminance : 0.0f;
            }
        }
    }
    return keys;
}

auto Lightmap_baker::debug_write_lightmap_png(const std::string& path) -> bool
{
    using namespace erhe::graphics;
    const int         width       = m_layout.width;
    const int         height      = m_layout.height;
    const std::size_t texel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<float> data;
    if (!read_lightmap(data)) {
        return false;
    }

    std::unique_ptr<Image_writer> writer = Image_writer::create();
    if (!writer || !writer->is_supported()) {
        return false;
    }
    // Reinhard + gamma for display.
    std::vector<std::uint8_t> pixels(texel_count * 4u);
    for (std::size_t i = 0; i < texel_count; ++i) {
        for (int c = 0; c < 3; ++c) {
            const float hdr = std::max(data[i * 4 + c], 0.0f);
            const float sdr = std::pow(hdr / (1.0f + hdr), 1.0f / 2.2f);
            pixels[i * 4 + c] = static_cast<std::uint8_t>(std::clamp(sdr, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
        pixels[i * 4 + 3] = (data[i * 4 + 3] > 0.0f) ? 255u : 0u;
    }
    return writer->write_png(
        std::filesystem::path{path},
        width,
        height,
        width * 4,
        erhe::dataformat::Format::format_8_vec4_unorm,
        std::span<const std::byte>{reinterpret_cast<const std::byte*>(pixels.data()), pixels.size()}
    );
}

} // namespace editor
