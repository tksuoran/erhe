#include "renderers/lightmap_baker.hpp"
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
#include <cmath>
#include <cstring>

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

// Per-draw UBO offsets must satisfy the device's uniform alignment; 256 is
// the specification maximum, valid everywhere.
constexpr std::size_t c_draw_ubo_stride = 256;

constexpr erhe::dataformat::Format c_position_format = erhe::dataformat::Format::format_32_vec4_float;
constexpr erhe::dataformat::Format c_normal_format   = erhe::dataformat::Format::format_32_vec4_float;
constexpr erhe::dataformat::Format c_albedo_format   = erhe::dataformat::Format::format_16_vec4_float;

// Per-tick ray budget for the interactive loop, as a texel count: the tile
// cursor walks the atlas in horizontal bands of at most this many texels
// per frame (whole-atlas sweeps on small pages, banded on large ones).
constexpr int c_texels_per_tick = 1 << 18;

// The lightmap G-buffer vertex shader positions triangles by their
// channel-2 UVs mapped into the region's atlas rect. Which NDC y lands in
// texture row 0 differs per backend (see Texture_renderer's Y_SIGN note);
// ERHE_LM_Y_SIGN keeps "row 0 is v = 0" everywhere.
constexpr const char* c_vertex_source = R"GLSL(
layout(location = 0) out vec3 v_position;
layout(location = 1) out vec3 v_normal;

void main()
{
    vec2 atlas_uv = a_texcoord_2 * lightmap_draw.uv_scale_offset.xy + lightmap_draw.uv_scale_offset.zw;
    vec2 ndc      = atlas_uv * 2.0 - 1.0 + lightmap_draw.jitter_ndc.xy;
    gl_Position   = vec4(ndc.x, ERHE_LM_Y_SIGN * ndc.y, 0.0, 1.0);
    v_position    = (lightmap_draw.world_from_node * vec4(a_position, 1.0)).xyz;
    v_normal      = normalize(mat3(lightmap_draw.world_from_node) * a_normal);
}
)GLSL";

constexpr const char* c_fragment_source = R"GLSL(
layout(location = 0) in vec3 v_position;
layout(location = 1) in vec3 v_normal;

void main()
{
    out_position = vec4(v_position, 1.0);          // w = coverage
    out_normal   = vec4(normalize(v_normal), 1.0); // w = coverage
    // Material base color factor (textures ignored for now): modulates
    // bounce radiance leaving this surface and later guides JNLM.
    out_albedo   = vec4(lightmap_draw.base_color.rgb, 1.0);
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
    vec3 p = position_coverage.xyz;
    vec3 n = normalize(texelFetch(s_normal, texel, 0).xyz);

    float ray_bias = lightmap_gather.ray_bias;
    vec3  direct   = vec3(0.0);
    for (uint i = 0u; i < lightmap_gather.light_count; ++i) {
        vec4 pos_type  = lightmap_gather.light_position_and_type[i];
        vec4 dir_cos   = lightmap_gather.light_direction_and_outer_cos[i];
        vec4 rad_range = lightmap_gather.light_radiance_and_range[i];
        vec4 params    = lightmap_gather.light_params[i];

        vec3  to_light;
        float attenuation = 1.0;
        float t_max       = 1.0e30;
        if (pos_type.w < 0.5) { // directional
            to_light = normalize(dir_cos.xyz);
        } else {
            vec3 d = pos_type.xyz - p;
            float distance = length(d);
            if (distance < 1.0e-6) {
                continue;
            }
            to_light    = d / distance;
            attenuation = 1.0 / max(distance * distance, 1.0e-4);
            t_max       = distance - ray_bias;
            if (pos_type.w > 1.5) { // spot: params.x = inner cos, dir_cos.w = outer cos
                float cos_angle = dot(to_light, normalize(dir_cos.xyz));
                attenuation *= smoothstep(dir_cos.w, params.x, cos_angle);
            }
        }
        float n_dot_l = dot(n, to_light);
        if ((n_dot_l <= 0.0) || (attenuation <= 0.0)) {
            continue;
        }
        vec3 origin = p + n * ray_bias;
        rayQueryEXT ray_query;
        // No backface culling: a receiver whose biased origin lands inside
        // a nearby occluder (floor texel under a resting object - the bias
        // exceeds the contact gap) must still see that occluder's interior,
        // or the contact ring bakes fully lit (observed light leak under
        // the torus). Self-hit defense comes from the normal-offset bias,
        // not from culling.
        rayQueryInitializeEXT(
            ray_query,
            s_tlas,
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT,
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
            rayQueryInitializeEXT(rq2, s_tlas, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFFu, p + n * lightmap_gather.ray_bias, 1.0e-4, to_light, 1.0e30);
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

    // One cosine-weighted hemisphere bounce ray. With cosine sampling the
    // irradiance estimator is E = pi * avg(L_in), and a diffuse hit's
    // outgoing radiance is albedo * E_hit / pi - the pi cancels, so each
    // sample adds albedo_hit * E_hit_published. Hits on non-lightmapped
    // instances (uv_scale_offset.x == 0) and interior/backface hits
    // contribute nothing.
    vec3 indirect = vec3(0.0);
    {
        uint  seed = pcg_hash(uint(texel.x) * 1973u + uint(texel.y) * 9277u + lightmap_gather.frame_index * 26699u);
        float u1   = rand_float(seed);
        float u2   = rand_float(seed);
        float r    = sqrt(u1);
        float phi  = 6.28318530718 * u2;
        vec3  t0   = normalize((abs(n.x) > 0.7) ? cross(n, vec3(0.0, 1.0, 0.0)) : cross(n, vec3(1.0, 0.0, 0.0)));
        vec3  t1   = cross(n, t0);
        vec3  bounce_dir = normalize(t0 * (r * cos(phi)) + t1 * (r * sin(phi)) + n * sqrt(max(0.0, 1.0 - u1)));

        rayQueryEXT bounce_query;
        rayQueryInitializeEXT(bounce_query, s_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, p + n * ray_bias, 1.0e-4, bounce_dir, 1.0e30);
        while (rayQueryProceedEXT(bounce_query)) {
        }
        if (rayQueryGetIntersectionTypeEXT(bounce_query, true) == gl_RayQueryCommittedIntersectionTriangleEXT) {
            uint instance_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(bounce_query, true));
            Lm_instance_record record = lm_instance.instances[instance_index];
            if (record.uv_scale_offset.x > 0.0) {
                // Reject interior hits: a bounce ray landing on a backface
                // is inside geometry and must contribute nothing.
                vec3 positions[3];
                rayQueryGetIntersectionTriangleVertexPositionsEXT(bounce_query, true, positions);
                mat4x3 world_from_object = rayQueryGetIntersectionObjectToWorldEXT(bounce_query, true);
                vec3 p0 = world_from_object * vec4(positions[0], 1.0);
                vec3 p1 = world_from_object * vec4(positions[1], 1.0);
                vec3 p2 = world_from_object * vec4(positions[2], 1.0);
                if (dot(cross(p1 - p0, p2 - p0), bounce_dir) <= 0.0) {
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
        }
    }

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
            Fragment_output{ .name = "out_position", .type = Glsl_type::float_vec4, .location = 0 },
            Fragment_output{ .name = "out_normal",   .type = Glsl_type::float_vec4, .location = 1 },
            Fragment_output{ .name = "out_albedo",   .type = Glsl_type::float_vec4, .location = 2 }
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
    pipeline_create_info.base.depth_stencil.depth_test_enable   = false;
    pipeline_create_info.base.depth_stencil.depth_write_enable  = false;
    pipeline_create_info.base.depth_stencil.stencil_test_enable = false;
    pipeline_create_info.base.bind_group_layout                 = m_bind_group_layout.get();
    pipeline_create_info.base.color_blend                       = &Color_blend_state::color_blend_disabled;
    pipeline_create_info.shader_stages                          = m_shader_stages.get();
    pipeline_create_info.vertex_input                           = vertex_input_entry.vertex_input.get();
    pipeline_create_info.color_attachment_count                 = 3;
    pipeline_create_info.color_attachment_formats[0]            = c_position_format;
    pipeline_create_info.color_attachment_formats[1]            = c_normal_format;
    pipeline_create_info.color_attachment_formats[2]            = c_albedo_format;
    for (int i = 0; i < 3; ++i) {
        pipeline_create_info.color_usage_before[i]              = Image_usage_flag_bit_mask::sampled;
        pipeline_create_info.color_usage_after[i]               = Image_usage_flag_bit_mask::sampled;
    }
    pipeline_create_info.sample_count                           = 1;

    m_pipeline = std::make_unique<Render_pipeline>(graphics_device, pipeline_create_info);
    if (!m_pipeline->is_valid()) {
        log_render->warn("Lightmap_baker: G-buffer pipeline is not valid");
        m_pipeline.reset();
    }

    // Gather: ray query in compute, exactly the machinery Ray_trace_renderer
    // proves out. The bounce ray additionally needs position fetch (backface
    // rejection) and buffer device addresses (texcoord-2 fetch). Absent
    // support, the layout + G-buffer still work; only baking is unavailable.
    if (!graphics_device.get_info().use_ray_query || !graphics_device.get_info().use_ray_tracing_position_fetch) {
        log_render->info("Lightmap_baker: ray query / position fetch not available, lightmap gather disabled");
        return;
    }

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
    m_gather_position_type_offset  = m_gather_block->add_vec4("light_position_and_type",       c_max_gather_lights)->get_offset_in_parent();
    m_gather_direction_cos_offset  = m_gather_block->add_vec4("light_direction_and_outer_cos", c_max_gather_lights)->get_offset_in_parent();
    m_gather_radiance_range_offset = m_gather_block->add_vec4("light_radiance_and_range",      c_max_gather_lights)->get_offset_in_parent();
    m_gather_params_offset         = m_gather_block->add_vec4("light_params",                  c_max_gather_lights)->get_offset_in_parent();
    m_gather_block_size            = m_gather_block->get_size_bytes(Shader_resource::Layout::std140);

    // Per-instance records (std430 SSBO) for the bounce ray's texcoord-2
    // fetch; layout verified against the C++ mirror below.
    m_lm_instance_struct = std::make_unique<Shader_resource>(graphics_device, "Lm_instance_record");
    const std::size_t off_index_address   = m_lm_instance_struct->add_uvec2("index_address"      )->get_offset_in_parent();
    const std::size_t off_vertex_address  = m_lm_instance_struct->add_uvec2("vertex_address"     )->get_offset_in_parent();
    const std::size_t off_stride          = m_lm_instance_struct->add_uint ("vertex_stride_uints")->get_offset_in_parent();
    m_lm_instance_struct->add_uint("pad0");
    m_lm_instance_struct->add_uint("pad1");
    m_lm_instance_struct->add_uint("pad2");
    const std::size_t off_uv_scale_offset = m_lm_instance_struct->add_vec4 ("uv_scale_offset"    )->get_offset_in_parent();
    ERHE_VERIFY(off_index_address   == offsetof(Lm_instance_record, index_address));
    ERHE_VERIFY(off_vertex_address  == offsetof(Lm_instance_record, vertex_address));
    ERHE_VERIFY(off_stride          == offsetof(Lm_instance_record, vertex_stride_uints));
    ERHE_VERIFY(off_uv_scale_offset == offsetof(Lm_instance_record, uv_scale_offset));
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
                // s_published vk 8. Raw bindings (TLAS, storage image) are
                // NOT offset, so the accumulation image sits at 9, clear of
                // every sampler's vk slot.
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
                Bind_group_layout_binding{
                    .binding_point = 9u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_accum",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                }
            },
            .debug_label       = erhe::utility::Debug_label{"lightmap gather layout"},
            .uses_texture_heap = false
        }
    );

    Shader_stages_create_info gather_create_info{
        .name             = "lightmap_gather",
        // Diagnostics: add { "ERHE_LM_DEBUG_GATHER", "1" } to defines to bake
        // R = closest blocking hit t, G = max NdotL, B = shadow miss ratio
        // instead of irradiance (see the debug block in c_gather_source).
        .defines          = {
            { "ERHE_LM_TEXCOORD2_OFFSET", fmt::format("{}", texcoord2.attribute->offset / 4) }
        },
        .extensions       = {
            { Shader_type::compute_shader, "GL_EXT_ray_query" },
            { Shader_type::compute_shader, "GL_EXT_ray_tracing_position_fetch" },
            { Shader_type::compute_shader, "GL_EXT_buffer_reference" },
            { Shader_type::compute_shader, "GL_EXT_buffer_reference_uvec2" }
        },
        .struct_types     = { m_lm_instance_struct.get() },
        .interface_blocks = { m_gather_block.get(), m_lm_instance_block.get() },
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_gather_source} }
        },
        .bind_group_layout = m_gather_layout.get()
    };
    Shader_stages_prototype gather_prototype = build_shader_stages(graphics_device, gather_create_info);
    if (!gather_prototype.is_valid()) {
        log_render->warn("Lightmap_baker: gather shader failed to compile/link");
        return;
    }
    m_gather_shader_stages = std::make_unique<Shader_stages>(graphics_device, std::move(gather_prototype));
    m_gather_pipeline = std::make_unique<Compute_pipeline>(
        graphics_device,
        Compute_pipeline_data{
            .name              = "lightmap_gather",
            .shader_stages     = m_gather_shader_stages.get(),
            .bind_group_layout = m_gather_layout.get()
        }
    );
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
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 1u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_dst",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
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

    // Resolve (accum running average -> published atlas); same layout shape
    // as dilate (i_src / i_dst rgba32f storage images).
    Shader_stages_create_info resolve_create_info{
        .name    = "lightmap_resolve",
        .shaders = {
            { Shader_type::compute_shader, std::string_view{c_resolve_source} }
        },
        .bind_group_layout = m_dilate_layout.get()
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
            .bind_group_layout = m_dilate_layout.get()
        }
    );
}

Lightmap_baker::~Lightmap_baker() noexcept = default;

auto Lightmap_baker::is_supported() const -> bool
{
    return static_cast<bool>(m_pipeline);
}

auto Lightmap_baker::update_layout(Scene_root& scene_root, const float texels_per_meter) -> bool
{
    m_layout            = Atlas_layout{};
    m_gbuffer_valid     = false;
    m_lightmap_valid    = false;
    m_regions_published = false;
    m_accum_cleared     = false;
    m_layout_scene_root = &scene_root;

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

void Lightmap_baker::ensure_gbuffer_targets()
{
    using namespace erhe::graphics;
    const bool matches =
        m_position_texture &&
        (m_position_texture->get_width()  == m_layout.width) &&
        (m_position_texture->get_height() == m_layout.height);
    if (matches) {
        return;
    }
    const auto make_target = [this](const char* label, erhe::dataformat::Format format) {
        return std::make_shared<Texture>(
            m_graphics_device,
            Texture_create_info{
                .device      = m_graphics_device,
                .usage_mask  =
                    Image_usage_flag_bit_mask::color_attachment |
                    Image_usage_flag_bit_mask::sampled          |
                    Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    };
    m_position_texture = make_target("lightmap gbuffer position", c_position_format);
    m_normal_texture   = make_target("lightmap gbuffer normal",   c_normal_format);
    m_albedo_texture   = make_target("lightmap gbuffer albedo",   c_albedo_format);
    m_gbuffer_valid    = false;
}

auto Lightmap_baker::bake_gbuffer() -> bool
{
    using namespace erhe::graphics;

    if (!m_pipeline || (m_layout.width == 0) || m_layout.regions.empty()) {
        return false;
    }

    const std::size_t not_skinned_key = m_mesh_memory.get_vertex_input_from_vertex_format(m_mesh_memory.vertex_format_not_skinned).key;

    ensure_gbuffer_targets();

    // Multi-jitter conservative coverage (plan phase 2, Bakery-style): each
    // region rasterizes 9 times with sub-texel NDC offsets, center pass
    // LAST. Depth test is off, so later draws win: edge texels whose center
    // just misses every triangle still get a jittered write, and properly
    // covered texels end with the unjittered value.
    constexpr int c_jitter_count = 9;
    constexpr float c_jitter[c_jitter_count][2] = {
        {-1.0f, -1.0f}, {0.0f, -1.0f}, {1.0f, -1.0f},
        {-1.0f,  0.0f},                {1.0f,  0.0f},
        {-1.0f,  1.0f}, {0.0f,  1.0f}, {1.0f,  1.0f},
        { 0.0f,  0.0f} // center last
    };

    // Per-draw UBO: one record per region per jitter pass.
    const std::size_t ubo_bytes = m_layout.regions.size() * c_jitter_count * c_draw_ubo_stride;
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
        // Half-texel jitter magnitude in NDC ([-1,1] spans the page).
        const float jitter_step_x = 0.5f * 2.0f / static_cast<float>(m_layout.width);
        const float jitter_step_y = 0.5f * 2.0f / static_cast<float>(m_layout.height);
        const std::span<std::byte> mapped = draw_ubo.map_bytes(0, ubo_bytes);
        std::memset(mapped.data(), 0, ubo_bytes);
        for (int j = 0; j < c_jitter_count; ++j) {
            const glm::vec4 jitter_ndc{c_jitter[j][0] * jitter_step_x, c_jitter[j][1] * jitter_step_y, 0.0f, 0.0f};
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
                const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
                const glm::mat4 world_from_node = (node != nullptr) ? node->world_from_node() : glm::mat4{1.0f};
                glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
                if (region.mesh) {
                    const std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_primitives();
                    if ((region.primitive_index < primitives.size()) && primitives[region.primitive_index].material) {
                        base_color = glm::vec4{primitives[region.primitive_index].material->data.base_color, 1.0f};
                    }
                }
                std::byte* const record = mapped.data() + (static_cast<std::size_t>(j) * m_layout.regions.size() + i) * c_draw_ubo_stride;
                std::memcpy(record + m_draw_block_world_offset,      &world_from_node,        sizeof(glm::mat4));
                std::memcpy(record + m_draw_block_uv_offset,         &region.uv_scale_offset, sizeof(glm::vec4));
                std::memcpy(record + m_draw_block_jitter_offset,     &jitter_ndc,             sizeof(glm::vec4));
                std::memcpy(record + m_draw_block_base_color_offset, &base_color,             sizeof(glm::vec4));
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
    command_buffer.transition_texture_layout(*m_position_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_normal_texture,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_albedo_texture,   Image_layout::shader_read_only_optimal);

    Texture* const gbuffer_targets[3] = { m_position_texture.get(), m_normal_texture.get(), m_albedo_texture.get() };
    Render_pass_descriptor descriptor{};
    for (int i = 0; i < 3; ++i) {
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
    descriptor.render_target_width  = m_layout.width;
    descriptor.render_target_height = m_layout.height;
    descriptor.debug_label = erhe::utility::Debug_label{"lightmap gbuffer"};

    std::size_t drawn = 0;
    {
        Render_pass            render_pass{m_graphics_device, descriptor};
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        const Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, m_layout.width, m_layout.height);
        encoder.set_scissor_rect (0, 0, m_layout.width, m_layout.height);
        encoder.set_bind_group_layout(m_bind_group_layout.get());
        encoder.set_render_pipeline(*m_pipeline);

        for (int j = 0; j < c_jitter_count; ++j) {
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
                if (!region.mesh) {
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
                const std::size_t record_index = static_cast<std::size_t>(j) * m_layout.regions.size() + i;
                encoder.set_buffer(Buffer_target::uniform, &draw_ubo, record_index * c_draw_ubo_stride, m_draw_block_size, 0);

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
                if (j == 0) {
                    ++drawn;
                }
            }
        }
    }
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_gbuffer_valid = drawn > 0;
    log_render->info("Lightmap_baker: G-buffer baked, {} of {} regions drawn, {}x{}", drawn, m_layout.regions.size(), m_layout.width, m_layout.height);
    return m_gbuffer_valid;
}

auto Lightmap_baker::debug_write_gbuffer_pngs(const std::string& base_path) -> bool
{
    using namespace erhe::graphics;
    if (!m_gbuffer_valid || !m_position_texture || !m_normal_texture) {
        return false;
    }
    const int         width         = m_layout.width;
    const int         height        = m_layout.height;
    const std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 16u; // rgba32f
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

    const auto read_texture = [&](Texture& texture, std::vector<float>& out_data) -> bool {
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
                .debug_label = erhe::utility::Debug_label{"lightmap gbuffer readback"}
            }
        };
        constexpr unsigned int bake_thread_slot = 6;
        Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
        command_buffer.begin();
        command_buffer.transition_texture_layout(texture, Image_layout::transfer_src_optimal);
        {
            Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
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
        m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
        m_graphics_device.wait_idle();
        const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
        out_data.resize(texel_count * 4u);
        std::memcpy(out_data.data(), mapped.data(), byte_count);
        readback.unmap();
        return true;
    };

    std::vector<float> position_data;
    std::vector<float> normal_data;
    if (!read_texture(*m_position_texture, position_data) || !read_texture(*m_normal_texture, normal_data)) {
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
    return position_ok && normal_ok;
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
    const bool matches =
        m_lightmap_texture &&
        (m_lightmap_texture->get_width()  == m_layout.width) &&
        (m_lightmap_texture->get_height() == m_layout.height);
    if (!matches) {
        const auto make_storage = [this](const char* label, const uint64_t usage_mask) {
            return std::make_shared<Texture>(
                m_graphics_device,
                Texture_create_info{
                    .device      = m_graphics_device,
                    .usage_mask  = usage_mask,
                    .type        = Texture_type::texture_2d,
                    .pixelformat = erhe::dataformat::Format::format_32_vec4_float,
                    .width       = m_layout.width,
                    .height      = m_layout.height,
                    .debug_label = erhe::utility::Debug_label{label}
                }
            );
        };
        // Published atlas (sampled by the forward renderer and by bounce
        // rays), accumulation atlas (sum + count) and the dilate scratch.
        m_lightmap_texture = make_storage(
            "lightmap atlas",
            Image_usage_flag_bit_mask::storage      |
            Image_usage_flag_bit_mask::sampled      |
            Image_usage_flag_bit_mask::transfer_src |
            Image_usage_flag_bit_mask::transfer_dst
        );
        m_accum_texture = make_storage(
            "lightmap accumulation",
            Image_usage_flag_bit_mask::storage |
            Image_usage_flag_bit_mask::transfer_dst
        );
        m_dilate_texture = make_storage("lightmap dilate scratch", Image_usage_flag_bit_mask::storage);
        m_accum_cleared = false;
    }
    if (!m_accum_cleared) {
        // Both start at zero: the accumulation restarts and the published
        // atlas must not feed garbage into bounce rays before the first
        // resolve.
        command_buffer.clear_texture(*m_accum_texture,    {0.0, 0.0, 0.0, 0.0});
        command_buffer.clear_texture(*m_lightmap_texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*m_accum_texture,    Image_layout::general);
        command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
        m_accum_cleared = true;
        m_cursor_y      = 0;
        m_sweep_count   = 0;
    }
}

void Lightmap_baker::publish_regions()
{
    // Per-primitive atlas regions so the forward renderer samples the bake
    // (Primitive_buffer uploads the value per draw; zero = no lightmap).
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh) {
            continue;
        }
        std::vector<erhe::scene::Mesh_primitive>& primitives = region.mesh->get_mutable_primitives();
        if (region.primitive_index < primitives.size()) {
            primitives[region.primitive_index].lightmap_uv_scale_offset = region.uv_scale_offset;
        }
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
    const float    ray_bias    = 0.01f;
    std::memcpy(data + m_gather_light_count_offset, &light_count,  sizeof(uint32_t));
    std::memcpy(data + m_gather_ray_bias_offset,    &ray_bias,     sizeof(float));
    std::memcpy(data + m_gather_frame_index_offset, &frame_index,  sizeof(uint32_t));
    std::memcpy(data + m_gather_base_y_offset,      &base_texel_y, sizeof(uint32_t));
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
    // radiance + albedo at the hit point.
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        if (!mesh || !mesh->is_visible() || mesh->skin) {
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
            if (buffer_mesh->vertex_buffer_ranges.size() >= 2) {
                const erhe::primitive::Buffer_range& attribute_range = buffer_mesh->vertex_buffer_ranges[1];
                const erhe::primitive::Buffer_range& index_range     = buffer_mesh->index_buffer_range;
                erhe::graphics::Buffer* attribute_buffer = m_mesh_memory.get_vertex_buffer(attribute_range);
                erhe::graphics::Buffer* index_buffer     = m_mesh_memory.get_index_buffer(index_range);
                if ((attribute_buffer != nullptr) && (index_buffer != nullptr) && ((attribute_range.element_size % 4) == 0)) {
                    const uint64_t attribute_base_address = attribute_buffer->get_device_address();
                    const uint64_t index_base_address     = index_buffer->get_device_address();
                    if ((attribute_base_address != 0) && (index_base_address != 0)) {
                        record.index_address       = index_base_address + index_range.byte_offset + (buffer_mesh->triangle_fill_indices.first_index * index_range.element_size);
                        record.vertex_address      = attribute_base_address + attribute_range.byte_offset;
                        record.vertex_stride_uints = static_cast<uint32_t>(attribute_range.element_size / 4);
                        for (const Instance_region& region : m_layout.regions) {
                            if ((region.mesh == mesh) && (region.primitive_index == primitive_index)) {
                                record.uv_scale_offset = region.uv_scale_offset;
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
                    // Gather rays trace without cull flags (shadow rays must
                    // see occluder interiors - the contact-leak fix; bounce
                    // rays reject backfaces manually), so instance-level
                    // facing-cull state is irrelevant; keep the default.
                    .disable_facing_cull   = false
                }
            );
        }
    }
}

auto Lightmap_baker::bake_direct(Scene_root& scene_root) -> bool
{
    using namespace erhe::graphics;

    if (!m_gather_pipeline || !m_resolve_pipeline || !m_gbuffer_valid || !m_position_texture) {
        return false;
    }

    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();

    ensure_bake_targets(command_buffer);
    // One-shot bake: restart accumulation so the result is exactly one
    // full-atlas sample - direct light plus one bounce off whatever the
    // published atlas held before this call (black on the first bake).
    command_buffer.clear_texture(*m_accum_texture, {0.0, 0.0, 0.0, 0.0});
    command_buffer.transition_texture_layout(*m_accum_texture, Image_layout::general);
    m_cursor_y    = 0;
    m_sweep_count = 0;

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

    // Gather one full-atlas sample: published atlas is sampled by bounce
    // rays (shader_read_only), accumulation image is written (general).
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_gather_layout.get());
        encoder.set_compute_pipeline(*m_gather_pipeline);
        encoder.set_buffer(Buffer_target::uniform, m_direct_gather_ubo.get(),    0, m_gather_block_size, 0);
        encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count,   1);
        encoder.set_acceleration_structure(2u, *m_tlas);
        encoder.set_sampled_image(3u, *m_position_texture, *m_nearest_sampler);
        encoder.set_sampled_image(4u, *m_normal_texture,   *m_nearest_sampler);
        encoder.set_sampled_image(5u, *m_albedo_texture,   *m_linear_sampler);
        encoder.set_sampled_image(6u, *m_lightmap_texture, *m_linear_sampler);
        encoder.set_storage_image(9u, *m_accum_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
            (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
            1
        );
    }

    record_resolve_and_dilate(command_buffer);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_lightmap_valid = true;
    m_sweep_count    = 1;
    publish_regions();

    log_render->info(
        "Lightmap_baker: direct light baked, {} lights, {} occluder instances, {}x{}",
        lights.size(), m_tick_instances.size(), m_layout.width, m_layout.height
    );
    return true;
}

void Lightmap_baker::record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;

    // Resolve the running average into the published atlas, then dilate it
    // (even iteration count >= s_padding so the final pass lands back in
    // m_lightmap_texture); never touches the accumulation buffer.
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::general);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_dilate_layout.get());
        encoder.set_compute_pipeline(*m_resolve_pipeline);
        encoder.set_storage_image(0u, *m_accum_texture);
        encoder.set_storage_image(1u, *m_lightmap_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
            (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
            1
        );
    }
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    if (m_dilate_pipeline && m_dilate_texture) {
        command_buffer.transition_texture_layout(*m_dilate_texture, Image_layout::general);
        constexpr int dilate_iterations = 2 * ((s_padding + 1) / 2);
        Texture* const ping[2] = { m_lightmap_texture.get(), m_dilate_texture.get() };
        for (int i = 0; i < dilate_iterations; ++i) {
            {
                Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
                encoder.set_bind_group_layout(m_dilate_layout.get());
                encoder.set_compute_pipeline(*m_dilate_pipeline);
                encoder.set_storage_image(0u, *ping[i & 1]);
                encoder.set_storage_image(1u, *ping[(i + 1) & 1]);
                encoder.dispatch_compute(
                    (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
                    (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
                    1
                );
            }
            command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
        }
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
}

void Lightmap_baker::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root, const float texels_per_meter)
{
    using namespace erhe::graphics;

    if (!m_gather_pipeline || !m_resolve_pipeline) {
        return;
    }

    // Change detection (plan section 3a step 1), cheapest response first.
    // Three tiers of FNV hashes over the bake inputs:
    //   layout   - the lightmapped set + texel density -> redo atlas layout
    //   gbuffer  - lightmapped region transforms       -> re-raster G-buffer
    //   lighting - lights + occluder transforms        -> reset accumulation
    // Each tier implies the tiers below it.
    uint64_t hash_layout = fnv1a64(&texels_per_meter, sizeof(float));
    {
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
    }
    const auto region_hash = [this]() -> uint64_t {
        uint64_t hash = 0xcbf29ce484222325ull;
        for (const Instance_region& region : m_layout.regions) {
            const erhe::scene::Node* const node = region.mesh ? region.mesh->get_node() : nullptr;
            if (node != nullptr) {
                const glm::mat4 world_from_node = node->world_from_node();
                hash = fnv1a64(&world_from_node, sizeof(world_from_node), hash);
            }
        }
        return hash;
    };
    uint64_t hash_gbuffer = region_hash();
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
    for (const std::shared_ptr<erhe::scene::Mesh>& mesh : scene_root.layers().content()->meshes) {
        // Occluder set: any visible content mesh shadows the bake, so its
        // motion invalidates accumulated visibility.
        if (!mesh || !mesh->is_visible() || mesh->skin) {
            continue;
        }
        const erhe::scene::Node* const node = mesh->get_node();
        if (node == nullptr) {
            continue;
        }
        const glm::mat4 world_from_node = node->world_from_node();
        hash_lighting = fnv1a64(&world_from_node, sizeof(world_from_node), hash_lighting);
    }

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
            if (!update_layout(scene_root, texels_per_meter)) {
                return;
            }
            // Regions changed; re-derive their transform hash so the next
            // tick does not see a spurious G-buffer invalidation.
            hash_gbuffer = region_hash();
            reset = true;
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
    m_layout_texels_per_meter = texels_per_meter;

    if (m_layout.width == 0) {
        if (!update_layout(scene_root, texels_per_meter)) {
            return;
        }
    }
    if (!m_gbuffer_valid) {
        // Standalone submit (wait_idle): a hitch, but only on invalidation
        // events (transform edit of a lightmapped mesh), not steady state.
        if (!bake_gbuffer()) {
            return;
        }
        m_hash_gbuffer = region_hash();
        reset          = true;
    }

    ensure_bake_targets(command_buffer);
    if (reset && m_accum_cleared) {
        command_buffer.clear_texture(*m_accum_texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*m_accum_texture, Image_layout::general);
        m_cursor_y    = 0;
        m_sweep_count = 0;
    }
    m_reset_requested = false;

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

    // Budgeted band (plan section 3a step 2): the tile cursor walks the
    // atlas top to bottom; small pages sweep whole-atlas every frame.
    const int rows_budget = std::clamp(c_texels_per_tick / std::max(m_layout.width, 1), 8, m_layout.height);
    const int band_rows   = std::min(rows_budget, m_layout.height - m_cursor_y);
    const uint32_t base_y = static_cast<uint32_t>(m_cursor_y);

    Ring_buffer_range ubo_range = m_tick_gather_ubo->acquire(Ring_buffer_usage::CPU_write, m_gather_block_size);
    {
        std::span<std::byte> gpu_data = ubo_range.get_span();
        write_gather_ubo(gpu_data.data(), lights, m_frame_counter, base_y);
        ubo_range.bytes_written(m_gather_block_size);
        ubo_range.close();
    }
    const std::size_t record_byte_count = m_tick_records.size() * sizeof(Lm_instance_record);
    Ring_buffer_range ssbo_range = m_tick_instance_ssbo->acquire(Ring_buffer_usage::CPU_write, record_byte_count);
    {
        std::span<std::byte> gpu_data = ssbo_range.get_span();
        std::memcpy(gpu_data.data(), m_tick_records.data(), record_byte_count);
        ssbo_range.bytes_written(record_byte_count);
        ssbo_range.close();
    }

    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    {
        Compute_command_encoder encoder = m_graphics_device.make_compute_command_encoder(command_buffer);
        encoder.set_bind_group_layout(m_gather_layout.get());
        encoder.set_compute_pipeline(*m_gather_pipeline);
        m_tick_gather_ubo->bind(encoder, ubo_range);
        m_tick_instance_ssbo->bind(encoder, ssbo_range);
        encoder.set_acceleration_structure(2u, *slot.acceleration_structure);
        encoder.set_sampled_image(3u, *m_position_texture, *m_nearest_sampler);
        encoder.set_sampled_image(4u, *m_normal_texture,   *m_nearest_sampler);
        encoder.set_sampled_image(5u, *m_albedo_texture,   *m_linear_sampler);
        encoder.set_sampled_image(6u, *m_lightmap_texture, *m_linear_sampler);
        encoder.set_storage_image(9u, *m_accum_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width) + 7) / 8,
            (static_cast<std::uintptr_t>(band_rows)      + 7) / 8,
            1
        );
    }
    ubo_range.release();
    ssbo_range.release();

    record_resolve_and_dilate(command_buffer);
    m_lightmap_valid = true;
    if (!m_regions_published) {
        publish_regions();
    }

    m_cursor_y += band_rows;
    if (m_cursor_y >= m_layout.height) {
        m_cursor_y = 0;
        ++m_sweep_count;
    }
    ++m_frame_counter;
}

auto Lightmap_baker::debug_write_lightmap_png(const std::string& path) -> bool
{
    using namespace erhe::graphics;
    if (!m_lightmap_valid || !m_lightmap_texture) {
        return false;
    }
    const int         width         = m_layout.width;
    const int         height        = m_layout.height;
    const std::size_t texel_count   = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    const std::size_t bytes_per_row = static_cast<std::size_t>(width) * 16u;
    const std::size_t byte_count    = bytes_per_row * static_cast<std::size_t>(height);

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
            .debug_label = erhe::utility::Debug_label{"lightmap readback"}
        }
    };
    constexpr unsigned int bake_thread_slot = 6;
    Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
    command_buffer.begin();
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            m_lightmap_texture.get(),
            0, 0,
            glm::ivec3{0, 0, 0},
            glm::ivec3{width, height, 1},
            &readback,
            0,
            bytes_per_row,
            byte_count
        );
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    std::vector<float> data(texel_count * 4u);
    {
        const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
        std::memcpy(data.data(), mapped.data(), byte_count);
        readback.unmap();
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
