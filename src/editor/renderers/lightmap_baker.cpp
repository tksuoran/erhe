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
#include <cmath>
#include <cstdlib>
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

    vec3 direct = vec3(0.0);
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
        vec3 origin = adaptive_offset(p, n);
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

        rayQueryEXT bounce_query;
        rayQueryInitializeEXT(bounce_query, s_tlas, gl_RayFlagsOpaqueEXT, 0xFFu, adaptive_offset(p, n), 1.0e-4, bounce_dir, 1.0e30);
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
    // texel a chart triangle touches gets a fragment in ONE pass; the 9-tap
    // jitter loop in bake_gbuffer() is the fallback without the extension.
    m_conservative_raster = graphics_device.get_info().use_conservative_rasterization;
    pipeline_create_info.base.rasterization.conservative_enable = m_conservative_raster;
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
                // s_sky_multiscatter vk 10. Raw bindings (TLAS, storage
                // image) are NOT offset, so the accumulation image sits at
                // 11, clear of every sampler's vk slot.
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
                Bind_group_layout_binding{
                    .binding_point = 11u,
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
        // Environment ERHE_LM_NO_INDIRECT=1 disables the bounce ray so the
        // atlas holds pure direct irradiance (isolates bounce defects).
        .defines          = [&]() {
            std::vector<std::pair<std::string, std::string>> defines{
                { "ERHE_LM_TEXCOORD2_OFFSET",   fmt::format("{}", texcoord2.attribute->offset / 4) },
                { "ERHE_RT_HAS_POSITION_FETCH", use_position_fetch ? "1" : "0" }
            };
            const char* const no_indirect = std::getenv("ERHE_LM_NO_INDIRECT");
            if ((no_indirect != nullptr) && (no_indirect[0] == '1')) {
                log_render->warn("Lightmap_baker: ERHE_LM_NO_INDIRECT=1 - bounce ray disabled");
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
                    .image_format  = "rgba32f",
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
                },
                Bind_group_layout_binding{
                    .binding_point = 2u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_normal",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba32f",
                    .stage_flags   = Shader_stage_flags::compute
                },
                Bind_group_layout_binding{
                    .binding_point = 3u,
                    .type          = Binding_type::storage_image,
                    .name          = "i_albedo",
                    .glsl_type     = Glsl_type::image_2d,
                    .image_format  = "rgba16f",
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
    seam_pipeline_create_info.color_attachment_formats[0]            = erhe::dataformat::Format::format_32_vec4_float;
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

auto Lightmap_baker::update_layout(Scene_root& scene_root, const float texels_per_meter, const float min_face_texels) -> bool
{
    m_layout            = Atlas_layout{};
    m_gbuffer_valid     = false;
    m_lightmap_valid    = false;
    m_regions_published = false;
    m_accum_cleared     = false;
    m_layout_scene_root = &scene_root;

    // Rejection counters for the zero-regions diagnostic below - "the atlas
    // is empty" is otherwise invisible (every filter is a silent continue).
    std::size_t seen_meshes       = 0;
    std::size_t skip_not_flagged  = 0;
    std::size_t skip_no_shape     = 0;
    std::size_t skip_no_uvs       = 0;

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
            // Only primitives that have lightmap UVs (channel 2) participate;
            // Generate Lightmap UVs in the Lightmap window produces them.
            erhe::geometry::Mesh_attributes& attributes = geometry->get_attributes();
            if (!attributes.corner_texcoord_2.has(0)) {
                ++skip_no_uvs;
                continue;
            }
            Instance_region region;
            region.mesh            = mesh;
            region.primitive_index = primitive_index;
            region.world_area      = mesh_surface_area(geometry->get_mesh()) * instance_area_scale;
            // Chart-space coverage: summed facet UV area (fan triangles) in
            // the [0,1]^2 chart space. Dividing the region area by this
            // makes texels-per-meter exact per facet: a facet's texels =
            // uv_area_facet * side^2 = (world_area_facet / world_area *
            // coverage_share...) - concretely, side = sqrt(world_area /
            // coverage) * density gives every facet world_area_facet *
            // density^2 texels regardless of gutters, packing waste, or the
            // packer's minimum-chart-size upscales.
            {
                const GEO::Mesh& geo_mesh = geometry->get_mesh();
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
                // Floor at 5% (worst allowed boost ~4.5x per axis): lower
                // coverage means broken or absurdly gutter-dominated UVs,
                // where growing the region without bound would explode the
                // page instead of fixing anything.
                region.uv_coverage         = std::clamp(coverage, 0.05f, 1.0f);
                region.min_facet_uv_extent = (min_facet_extent < std::numeric_limits<float>::max()) ? min_facet_extent : 1.0f;
            }
            regions.push_back(std::move(region));
        }
    }
    if (regions.empty()) {
        log_render->info(
            "Lightmap_baker::update_layout: no regions to pack (content meshes {}, skipped: not lightmapped {}, no render shape/geometry {}, no lightmap UVs {})",
            seen_meshes,
            skip_not_flagged,
            skip_no_shape,
            skip_no_uvs
        );
        return false;
    }

    // Region content side in texels; the normalized per-mesh chart set is
    // square, so the region is too.
    const auto side_of = [texels_per_meter, min_face_texels](const Instance_region& region) -> int {
        float side = std::sqrt(std::max(region.world_area, 0.0f) / region.uv_coverage) * texels_per_meter;
        // Min-face-texels bound: grow the region until its smallest facet
        // spans min_face_texels on its shorter UV axis. This is the half of
        // the minimum-size guarantee the unwrap cannot provide (see
        // Instance_region::min_facet_uv_extent). Capped at 4x the density
        // side so one degenerate sliver facet cannot explode the page.
        if ((min_face_texels > 0.0f) && (region.min_facet_uv_extent > 0.0f)) {
            const float bound = min_face_texels / region.min_facet_uv_extent;
            side = std::max(side, std::min(bound, 4.0f * side));
        }
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
        build_seam_vertices();
        return true;
    }
    // Even the largest page failed; drop the layout (a later change can
    // add multi-page support - plan keeps pages <= 4096^2). Report why:
    // this is usually the density - a single large mesh (e.g. a floor)
    // at high texels_per_meter needs a region bigger than the max page.
    log_render->info(
        "Lightmap_baker::update_layout: {} regions do not fit the maximum {}x{} page at {} texels/m (largest region side {} texels) - lower the density",
        regions.size(),
        s_max_page,
        s_max_page,
        texels_per_meter,
        regions.empty() ? 0 : side_of(regions.front())
    );
    m_seam_vertices.clear();
    return false;
}

void Lightmap_baker::build_seam_vertices()
{
    m_seam_vertices.clear();
    std::size_t seam_count = 0;
    for (const Instance_region& region : m_layout.regions) {
        if (!region.mesh) {
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
    log_render->info("Lightmap_baker: {} seam edges ({} line vertices)", seam_count, m_seam_vertices.size());
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
                // storage: the virtual-offset adjust pass rewrites the
                // position G-buffer in place (reads normal the same way).
                .usage_mask  =
                    Image_usage_flag_bit_mask::color_attachment |
                    Image_usage_flag_bit_mask::sampled          |
                    Image_usage_flag_bit_mask::storage          |
                    Image_usage_flag_bit_mask::transfer_src,
                .type        = Texture_type::texture_2d,
                .pixelformat = format,
                .width       = m_layout.width,
                .height      = m_layout.height,
                .debug_label = erhe::utility::Debug_label{label}
            }
        );
    };
    m_position_texture        = make_target("lightmap gbuffer position",        c_position_format);
    m_normal_texture          = make_target("lightmap gbuffer normal",          c_normal_format);
    m_albedo_texture          = make_target("lightmap gbuffer albedo",          c_albedo_format);
    m_smooth_position_texture = make_target("lightmap gbuffer smooth position", c_position_format);
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

    // Conservative coverage. Preferred: native conservative rasterization
    // (pipeline flag, one unjittered pass - the LAST jitter entry is the
    // center tap). Fallback: multi-jitter re-render (Bakery-style) - each
    // region rasterizes 9 times with sub-texel NDC offsets, center pass
    // LAST. Depth test is off, so later draws win: edge texels whose center
    // just misses every triangle still get a jittered write, and properly
    // covered texels end with the unjittered value.
    constexpr int c_jitter_count = 9;
    const int first_jitter_pass = m_conservative_raster ? (c_jitter_count - 1) : 0;
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
        for (int j = first_jitter_pass; j < c_jitter_count; ++j) {
            const glm::vec4 jitter_ndc{c_jitter[j][0] * jitter_step_x, c_jitter[j][1] * jitter_step_y, 0.0f, 0.0f};
            for (std::size_t i = 0; i < m_layout.regions.size(); ++i) {
                const Instance_region& region = m_layout.regions[i];
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

        for (int j = first_jitter_pass; j < c_jitter_count; ++j) {
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
                if (j == first_jitter_pass) {
                    ++drawn;
                }
            }
        }
    }
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_gbuffer_valid    = drawn > 0;
    m_gbuffer_adjusted = false; // fresh positions need the virtual-offset pass
    log_render->info(
        "Lightmap_baker: G-buffer baked, {} of {} regions drawn, {}x{}, {}",
        drawn, m_layout.regions.size(), m_layout.width, m_layout.height,
        m_conservative_raster ? "native conservative raster" : "9-tap jitter fallback"
    );
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
    std::vector<float> smooth_data;
    if (!read_texture(*m_position_texture, position_data) || !read_texture(*m_normal_texture, normal_data) || !read_texture(*m_smooth_position_texture, smooth_data)) {
        return false;
    }
    {
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
    // Albedo readback (RGBA16F target): copy through an RGBA32F-sized
    // buffer is wrong for 16F; instead reuse the debug path by sampling is
    // overkill - blit copy gives raw half floats, so decode manually.
    std::vector<float> albedo_data;
    {
        const std::size_t half_bytes_per_row = static_cast<std::size_t>(width) * 8u; // rgba16f
        const std::size_t half_byte_count    = half_bytes_per_row * static_cast<std::size_t>(height);
        Buffer readback{
            m_graphics_device,
            Buffer_create_info{
                .capacity_byte_count                    = half_byte_count,
                .memory_allocation_create_flag_bit_mask = Memory_allocation_create_flag_bit_mask::mapped,
                .usage                                  = Buffer_usage::transfer_dst | Buffer_usage::storage,
                .required_memory_property_bit_mask      =
                    Memory_property_flag_bit_mask::host_read |
                    Memory_property_flag_bit_mask::host_write,
                .preferred_memory_property_bit_mask     =
                    Memory_property_flag_bit_mask::host_coherent |
                    Memory_property_flag_bit_mask::host_persistent,
                .debug_label = erhe::utility::Debug_label{"lightmap albedo readback"}
            }
        };
        constexpr unsigned int bake_thread_slot = 6;
        Command_buffer& command_buffer = m_graphics_device.get_command_buffer(bake_thread_slot);
        command_buffer.begin();
        command_buffer.transition_texture_layout(*m_albedo_texture, Image_layout::transfer_src_optimal);
        {
            Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
            blit.copy_from_texture(m_albedo_texture.get(), 0, 0, glm::ivec3{0, 0, 0}, glm::ivec3{width, height, 1}, &readback, 0, half_bytes_per_row, half_byte_count);
        }
        command_buffer.transition_texture_layout(*m_albedo_texture, Image_layout::shader_read_only_optimal);
        command_buffer.end();
        Command_buffer* command_buffers[] = { &command_buffer };
        m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
        m_graphics_device.wait_idle();
        const std::span<std::byte> mapped = readback.map_bytes(0, half_byte_count);
        const auto* halves = reinterpret_cast<const uint16_t*>(mapped.data());
        albedo_data.resize(texel_count * 4u);
        for (std::size_t i = 0; i < texel_count * 4u; ++i) {
            // Minimal half->float (no denormal/inf/nan care needed for albedo).
            const uint16_t h        = halves[i];
            const uint32_t sign     = static_cast<uint32_t>(h & 0x8000u) << 16;
            const uint32_t exponent = (h >> 10) & 0x1Fu;
            const uint32_t mantissa = h & 0x3FFu;
            uint32_t bits = 0;
            if (exponent != 0) {
                bits = sign | ((exponent + 112u) << 23) | (mantissa << 13);
            }
            float value;
            std::memcpy(&value, &bits, sizeof(float));
            albedo_data[i] = value;
        }
        readback.unmap();
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
            Image_usage_flag_bit_mask::storage          |
            Image_usage_flag_bit_mask::sampled          |
            Image_usage_flag_bit_mask::color_attachment | // seam blend line raster target
            Image_usage_flag_bit_mask::transfer_src     |
            Image_usage_flag_bit_mask::transfer_dst
        );
        m_accum_texture = make_storage(
            "lightmap accumulation",
            Image_usage_flag_bit_mask::storage |
            Image_usage_flag_bit_mask::transfer_dst
        );
        // The dilate scratch doubles as the seam pass sample source (a copy
        // of the published atlas, avoiding a read/write hazard like Godot's
        // light_accum_tex2).
        m_dilate_texture = make_storage(
            "lightmap dilate scratch",
            Image_usage_flag_bit_mask::storage |
            Image_usage_flag_bit_mask::sampled |
            Image_usage_flag_bit_mask::transfer_dst
        );
        // Renderer-facing double buffer: only complete publishes are copied
        // in, so accumulation resets never black out the sampled atlas.
        m_display_texture = make_storage(
            "lightmap display atlas",
            Image_usage_flag_bit_mask::sampled      |
            Image_usage_flag_bit_mask::transfer_src |
            Image_usage_flag_bit_mask::transfer_dst
        );
        m_accum_cleared = false;
    }
    if (!m_accum_cleared) {
        // All start at zero: the accumulation restarts, the working atlas
        // must not feed garbage into bounce rays before the first resolve,
        // and the display atlas holds no publish yet. This path runs only
        // on creation and layout repacks (update_layout drops
        // m_accum_cleared) - lighting/transform resets keep the display.
        command_buffer.clear_texture(*m_accum_texture,    {0.0, 0.0, 0.0, 0.0});
        command_buffer.clear_texture(*m_lightmap_texture, {0.0, 0.0, 0.0, 0.0});
        command_buffer.clear_texture(*m_display_texture,  {0.0, 0.0, 0.0, 0.0});
        command_buffer.transition_texture_layout(*m_accum_texture,    Image_layout::general);
        command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
        command_buffer.transition_texture_layout(*m_display_texture,  Image_layout::shader_read_only_optimal);
        m_accum_cleared  = true;
        m_display_valid  = false;
        m_cursor_y       = 0;
        m_sweep_count    = 0;
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

    // Virtual offset (one-shot per G-buffer): needs the TLAS just built.
    record_adjust(
        command_buffer,
        *m_tlas,
        [&](Compute_command_encoder& encoder) {
            encoder.set_buffer(Buffer_target::storage, m_direct_instance_ssbo.get(), 0, record_byte_count, 1);
        }
    );

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
        encoder.set_sampled_image(7u, (m_sky.transmittance_lut != nullptr) ? *m_sky.transmittance_lut : *m_albedo_texture, *m_linear_sampler);
        encoder.set_sampled_image(8u, (m_sky.multiscatter_lut  != nullptr) ? *m_sky.multiscatter_lut  : *m_albedo_texture, *m_linear_sampler);
        encoder.set_storage_image(11u, *m_accum_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
            (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
            1
        );
    }

    record_resolve_and_dilate(command_buffer, false);
    record_display_publish(command_buffer);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    m_lightmap_valid = true;
    m_display_valid  = true;
    m_sweep_count    = 1;
    publish_regions();

    log_render->info(
        "Lightmap_baker: direct light baked, {} lights, {} occluder instances, {}x{}",
        lights.size(), m_tick_instances.size(), m_layout.width, m_layout.height
    );
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
            (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
            (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
            1
        );
    }
    command_buffer.memory_barrier(Memory_barrier_mask::shader_image_access_barrier_bit);
    command_buffer.transition_texture_layout(*m_position_texture,        Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_normal_texture,          Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_smooth_position_texture, Image_layout::shader_read_only_optimal);
    m_gbuffer_adjusted = true;
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
    if ((options.denoise       != m_options.denoise      ) ||
        (options.dilation      != m_options.dilation     ) ||
        (options.seam_blend    != m_options.seam_blend   ) ||
        (options.gutter_texels != m_options.gutter_texels)) {
        m_publish_requested = true; // republish the current average with the new stages
    }
    m_options = options;
}

void Lightmap_baker::record_resolve_and_dilate(erhe::graphics::Command_buffer& command_buffer, const bool with_denoise)
{
    using namespace erhe::graphics;

    // Resolve the running average into the published atlas, optionally JNLM
    // denoise it, then dilate; never touches the accumulation buffer. The
    // dilation ping-pong iteration count is chosen so the final pass lands
    // back in m_lightmap_texture: without denoise it starts there (even
    // count), with denoise it starts in the scratch the denoiser wrote (odd
    // count).
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
                (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
                (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
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
                    (static_cast<std::uintptr_t>(m_layout.width)  + 7) / 8,
                    (static_cast<std::uintptr_t>(m_layout.height) + 7) / 8,
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
        record_seam_blend(command_buffer);
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
}

void Lightmap_baker::record_display_publish(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;
    if (!m_lightmap_texture || !m_display_texture) {
        return;
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    command_buffer.transition_texture_layout(*m_display_texture,  Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(m_lightmap_texture.get(), m_display_texture.get());
    }
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_display_texture,  Image_layout::shader_read_only_optimal);
}

void Lightmap_baker::record_seam_blend(erhe::graphics::Command_buffer& command_buffer)
{
    using namespace erhe::graphics;
    if (!m_seam_pipeline || m_seam_vertices.empty() || !m_dilate_texture) {
        return;
    }
    // Sample source: a copy of the published atlas in the dilate scratch
    // (rendering into the atlas while sampling it would be a hazard).
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::transfer_src_optimal);
    command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::transfer_dst_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(m_lightmap_texture.get(), m_dilate_texture.get());
    }
    command_buffer.transition_texture_layout(*m_dilate_texture,   Image_layout::shader_read_only_optimal);
    command_buffer.transition_texture_layout(*m_lightmap_texture, Image_layout::color_attachment_optimal);

    const std::size_t byte_count = m_seam_vertices.size() * sizeof(Seam_vertex);
    Ring_buffer_range vertex_range = m_seam_vertex_ring->acquire(Ring_buffer_usage::CPU_write, byte_count);
    {
        std::span<std::byte> gpu_data = vertex_range.get_span();
        std::memcpy(gpu_data.data(), m_seam_vertices.data(), byte_count);
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
    descriptor.render_target_width  = m_layout.width;
    descriptor.render_target_height = m_layout.height;
    descriptor.debug_label = erhe::utility::Debug_label{"lightmap seam blend"};
    {
        Render_pass            render_pass{m_graphics_device, descriptor};
        Render_command_encoder encoder = m_graphics_device.make_render_command_encoder(command_buffer);
        const Scoped_render_pass scoped{render_pass, command_buffer};
        encoder.set_viewport_rect(0, 0, m_layout.width, m_layout.height);
        encoder.set_scissor_rect (0, 0, m_layout.width, m_layout.height);
        encoder.set_bind_group_layout(m_seam_layout.get());
        encoder.set_render_pipeline(*m_seam_pipeline);
        encoder.set_sampled_image(0u, *m_dilate_texture, *m_linear_sampler);
        encoder.set_vertex_buffer(vertex_range.get_buffer()->get_buffer(), vertex_range.get_byte_start_offset_in_buffer(), 0);
        encoder.draw_primitives(Primitive_type::line, 0, m_seam_vertices.size());
    }
    vertex_range.release();
}

void Lightmap_baker::tick(erhe::graphics::Command_buffer& command_buffer, Scene_root& scene_root, const float texels_per_meter, const float min_face_texels)
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
            if (!update_layout(scene_root, texels_per_meter, min_face_texels)) {
                return;
            }
            // Regions changed; re-derive their transform hash so the next
            // tick does not see a spurious G-buffer invalidation.
            hash_gbuffer = region_hash();
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
    m_layout_texels_per_meter = texels_per_meter;

    if (m_layout.width == 0) {
        if (!update_layout(scene_root, texels_per_meter, min_face_texels)) {
            return;
        }
        m_gbuffer_upload_defer = true;
    }
    if (!m_gbuffer_valid) {
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
        encoder.set_sampled_image(7u, (m_sky.transmittance_lut != nullptr) ? *m_sky.transmittance_lut : *m_albedo_texture, *m_linear_sampler);
        encoder.set_sampled_image(8u, (m_sky.multiscatter_lut  != nullptr) ? *m_sky.multiscatter_lut  : *m_albedo_texture, *m_linear_sampler);
        encoder.set_storage_image(11u, *m_accum_texture);
        encoder.dispatch_compute(
            (static_cast<std::uintptr_t>(m_layout.width) + 7) / 8,
            (static_cast<std::uintptr_t>(band_rows)      + 7) / 8,
            1
        );
    }
    ubo_range.release();
    ssbo_range.release();

    m_cursor_y += band_rows;
    const bool sweep_completed = m_cursor_y >= m_layout.height;
    if (sweep_completed) {
        m_cursor_y = 0;
        ++m_sweep_count;
    }
    // Publish cadence (phase 4 denoise): during the first sweep after a
    // reset publish the raw average every tick so the lightmap appears
    // immediately; once a full sweep exists publish only on sweep
    // completion, with JNLM denoise folded in. Steady-state mid-sweep
    // ticks skip resolve+dilate entirely - the viewport (and the bounce
    // feedback) keep sampling the last denoised publish.
    if (m_sweep_count == 0) {
        record_resolve_and_dilate(command_buffer, false);
        // First-ever bake: nothing better to display, so copy the partial
        // sweep out for the progressive preview. After a reset the display
        // keeps the previous complete publish instead.
        if (!m_display_valid) {
            record_display_publish(command_buffer);
        }
    } else if (sweep_completed || m_publish_requested) {
        record_resolve_and_dilate(command_buffer, m_options.denoise);
        record_display_publish(command_buffer);
        m_display_valid = true;
    }
    m_publish_requested = false;
    m_lightmap_valid = true;
    if (!m_regions_published) {
        publish_regions();
    }
    ++m_frame_counter;
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
    command_buffer.transition_texture_layout(*source, Image_layout::transfer_src_optimal);
    {
        Blit_command_encoder blit = m_graphics_device.make_blit_command_encoder(command_buffer);
        blit.copy_from_texture(
            source,
            0, 0,
            glm::ivec3{0, 0, 0},
            glm::ivec3{width, height, 1},
            &readback,
            0,
            bytes_per_row,
            byte_count
        );
    }
    command_buffer.transition_texture_layout(*source, Image_layout::shader_read_only_optimal);
    command_buffer.end();
    Command_buffer* command_buffers[] = { &command_buffer };
    m_graphics_device.submit_command_buffers(std::span<Command_buffer* const>{command_buffers});
    m_graphics_device.wait_idle();

    out_rgba.resize(texel_count * 4u);
    {
        const std::span<std::byte> mapped = readback.map_bytes(0, byte_count);
        std::memcpy(out_rgba.data(), mapped.data(), byte_count);
        readback.unmap();
    }
    return true;
}

// Per-facet chart order keys (leak camouflage; see build_chart_order_keys
// in the header): mean baked luminance per facet, averaged over the covered
// texels of the facet's UV bounding box from a CPU readback of the
// published atlas (centroid texel as fallback for facets with no coverage).
auto Lightmap_baker::build_chart_order_keys() -> std::unordered_map<const erhe::geometry::Geometry*, std::vector<float>>
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
            const auto texel_x_of = [&region, page_width](const float u) -> int {
                const float atlas_u = u * region.uv_scale_offset.x + region.uv_scale_offset.z;
                return std::clamp(static_cast<int>(atlas_u * static_cast<float>(page_width)), 0, page_width - 1);
            };
            const auto texel_y_of = [&region, page_height](const float v) -> int {
                const float atlas_v = v * region.uv_scale_offset.y + region.uv_scale_offset.w;
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
