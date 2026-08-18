#ifndef ERHE_RAY_HIT_GLSL
#define ERHE_RAY_HIT_GLSL

// Shared ray-query hit fetch and surface shading, used by the GPU ray
// tracing renderer (ray_trace.comp) and by the DDGI probe trace
// (ddgi_trace.comp - doc/ddgi-plan.md).
//
// The includer must have declared, before including this file:
//  - the top level acceleration structure as "s_tlas"
//    (accelerationStructureEXT; erhe does not inject raw bindings),
//  - the instance record SSBO ("instance", from editor::Scene_tlas), the
//    material block and the light block (from Program_interface),
//  - the ERHE_RT_* stream-1 attribute offsets and ERHE_RT_HAS_POSITION_FETCH,
//  - the GL_EXT_ray_query / GL_EXT_buffer_reference extensions,
//  - the erhe_bxdf.glsl / erhe_light.glsl / erhe_texture.glsl includes.


// Raw uint view of a mesh memory pool, reached via the per-instance buffer
// device address (GL_EXT_buffer_reference + _uvec2). Addresses are stored
// pre-offset: index_address points at the instance's first triangle index,
// vertex_address at the start of its stream-1 vertex range.
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer Uint_data {
    uint data[];
};

const float c_t_min      = 0.001;
const float c_t_far      = 1.0e30;
// World-space origin offset along the geometric normal for continuation /
// shadow rays, to escape self-intersection with the surface just hit.
const float c_ray_offset = 1.0e-3;

// Instance mask bits (must match Ray_trace_renderer):
// bit 0 = every instance, bit 1 = non-transmissive instances only.
// Shadow rays use ERHE_RT_MASK_OPAQUE so glass does not cast shadows.
const uint c_mask_all    = 0x01u;
const uint c_mask_opaque = 0x02u;

// Everything shading needs about a committed triangle hit.
struct Hit_surface {
    vec3  position;         // world space
    vec3  shading_normal;   // world space, flipped to face the incoming ray
    vec3  geometric_normal; // world space, flipped to face the incoming ray
    bool  backface;         // ray hit the triangle from behind (inside a volume)
    vec4  tangent;          // world space xyz + handedness w (anisotropic BxDFs)
    vec2  texcoord;
    vec4  vertex_color;
    uint  material_index;
    float hit_t;
};

vec3 fetch_vec3(Uint_data vertices, uint base)
{
    return vec3(
        uintBitsToFloat(vertices.data[base + 0u]),
        uintBitsToFloat(vertices.data[base + 1u]),
        uintBitsToFloat(vertices.data[base + 2u])
    );
}

vec2 fetch_vec2(Uint_data vertices, uint base)
{
    return vec2(
        uintBitsToFloat(vertices.data[base + 0u]),
        uintBitsToFloat(vertices.data[base + 1u])
    );
}

vec4 fetch_vec4(Uint_data vertices, uint base)
{
    return vec4(
        uintBitsToFloat(vertices.data[base + 0u]),
        uintBitsToFloat(vertices.data[base + 1u]),
        uintBitsToFloat(vertices.data[base + 2u]),
        uintBitsToFloat(vertices.data[base + 3u])
    );
}

// Closest-hit trace + attribute/material fetch. Returns false on miss.
bool trace_closest(vec3 origin, vec3 direction, float t_max, out Hit_surface surface)
{
    rayQueryEXT ray_query;
    rayQueryInitializeEXT(ray_query, s_tlas, gl_RayFlagsOpaqueEXT, c_mask_all, origin, c_t_min, direction, t_max);
    while (rayQueryProceedEXT(ray_query)) {
        // All BLAS geometry is opaque; candidates are committed automatically.
        // Transparency is handled by continuation rays, not any-hit filtering.
    }
    if (rayQueryGetIntersectionTypeEXT(ray_query, true) != gl_RayQueryCommittedIntersectionTriangleEXT) {
        return false;
    }

    uint  instance_index = uint(rayQueryGetIntersectionInstanceCustomIndexEXT(ray_query, true));
    uint  primitive      = uint(rayQueryGetIntersectionPrimitiveIndexEXT(ray_query, true));
    vec2  barycentrics   = rayQueryGetIntersectionBarycentricsEXT(ray_query, true);
    float hit_t          = rayQueryGetIntersectionTEXT(ray_query, true);

    Instance_record record = instance.instances[instance_index];

    // Index values are relative to the instance's vertex range start (draws
    // use base_vertex the same way), so they index stream 1 directly.
    Uint_data indices  = Uint_data(record.index_address);
    Uint_data vertices = Uint_data(record.vertex_address);
    uint i0 = indices.data[3u * primitive + 0u];
    uint i1 = indices.data[3u * primitive + 1u];
    uint i2 = indices.data[3u * primitive + 2u];

    // Geometric normal from the committed triangle's object-space positions:
    // fetched from the acceleration structure when the backend supports
    // GL_EXT_ray_tracing_position_fetch, otherwise from the stream-0 pool
    // (the BLAS build input, so the values are identical) via the instance's
    // position_address.
    mat4x3 world_from_object = rayQueryGetIntersectionObjectToWorldEXT(ray_query, true);
    vec3 positions[3];
#if ERHE_RT_HAS_POSITION_FETCH
    rayQueryGetIntersectionTriangleVertexPositionsEXT(ray_query, true, positions);
#else
    Uint_data position_data = Uint_data(record.position_address);
    positions[0] = fetch_vec3(position_data, i0 * record.position_stride_uints);
    positions[1] = fetch_vec3(position_data, i1 * record.position_stride_uints);
    positions[2] = fetch_vec3(position_data, i2 * record.position_stride_uints);
#endif
    vec3 p0 = world_from_object * vec4(positions[0], 1.0);
    vec3 p1 = world_from_object * vec4(positions[1], 1.0);
    vec3 p2 = world_from_object * vec4(positions[2], 1.0);
    vec3 geometric_normal = normalize(cross(p1 - p0, p2 - p0));
    uint v0 = i0 * record.vertex_stride_uints;
    uint v1 = i1 * record.vertex_stride_uints;
    uint v2 = i2 * record.vertex_stride_uints;

    float w1 = barycentrics.x;
    float w2 = barycentrics.y;
    float w0 = 1.0 - w1 - w2;

    vec3 object_normal =
        w0 * fetch_vec3(vertices, v0 + uint(ERHE_RT_NORMAL_OFFSET)) +
        w1 * fetch_vec3(vertices, v1 + uint(ERHE_RT_NORMAL_OFFSET)) +
        w2 * fetch_vec3(vertices, v2 + uint(ERHE_RT_NORMAL_OFFSET));
    vec4 object_tangent =
        w0 * fetch_vec4(vertices, v0 + uint(ERHE_RT_TANGENT_OFFSET)) +
        w1 * fetch_vec4(vertices, v1 + uint(ERHE_RT_TANGENT_OFFSET)) +
        w2 * fetch_vec4(vertices, v2 + uint(ERHE_RT_TANGENT_OFFSET));
    vec2 texcoord =
        w0 * fetch_vec2(vertices, v0 + uint(ERHE_RT_TEXCOORD0_OFFSET)) +
        w1 * fetch_vec2(vertices, v1 + uint(ERHE_RT_TEXCOORD0_OFFSET)) +
        w2 * fetch_vec2(vertices, v2 + uint(ERHE_RT_TEXCOORD0_OFFSET));
    vec4 vertex_color =
        w0 * fetch_vec4(vertices, v0 + uint(ERHE_RT_COLOR0_OFFSET)) +
        w1 * fetch_vec4(vertices, v1 + uint(ERHE_RT_COLOR0_OFFSET)) +
        w2 * fetch_vec4(vertices, v2 + uint(ERHE_RT_COLOR0_OFFSET));

    // Normals transform with the inverse transpose: row-vector multiply by
    // world_to_object is the transpose multiply. Tangents transform like
    // directions (object-to-world linear part); handedness passes through.
    mat4x3 world_to_object = rayQueryGetIntersectionWorldToObjectEXT(ray_query, true);
    vec3 shading_normal = normalize(vec3(object_normal * world_to_object));
    vec3 world_tangent  = normalize(world_from_object * vec4(object_tangent.xyz, 0.0));

    // Two-sided: instances are built with facing cull disabled, so the ray
    // can hit either side. Flip both normals to face the incoming ray; the
    // backface flag preserves which side was hit (used for refraction eta).
    bool backface = dot(geometric_normal, direction) > 0.0;
    if (backface) {
        geometric_normal = -geometric_normal;
    }
    if (dot(shading_normal, direction) > 0.0) {
        shading_normal = -shading_normal;
    }

    surface.position         = origin + hit_t * direction;
    surface.shading_normal   = shading_normal;
    surface.geometric_normal = geometric_normal;
    surface.backface         = backface;
    surface.tangent          = vec4(world_tangent, object_tangent.w);
    surface.texcoord         = texcoord;
    surface.vertex_color     = vertex_color;
    surface.material_index   = record.material_index;
    surface.hit_t            = hit_t;
    return true;
}

// True when nothing opaque lies between the (offset) surface point and the
// light. Mask excludes transmissive instances so glass does not shadow.
bool light_visible(vec3 position, vec3 offset_normal, vec3 L, float t_max)
{
    rayQueryEXT ray_query;
    rayQueryInitializeEXT(
        ray_query,
        s_tlas,
        gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
        c_mask_opaque,
        position + offset_normal * c_ray_offset,
        c_t_min,
        L,
        t_max
    );
    while (rayQueryProceedEXT(ray_query)) {
    }
    return rayQueryGetIntersectionTypeEXT(ray_query, true) == gl_RayQueryCommittedIntersectionNoneEXT;
}

vec3 surface_base_color(Hit_surface surface, Material m)
{
    vec3 base_color = m.base_color.rgb * surface.vertex_color.rgb;
    base_color *= sample_texture_lod0(
        m.base_color_texture,
        surface.texcoord,
        m.base_color_rotation_scale,
        m.base_color_offset
    ).rgb;
    return base_color;
}

// Per-light BxDF, dispatched at runtime from material.bxdf_model (the
// raster path bakes the same selection compile-time via its variant axis;
// values match erhe::primitive::Bxdf_model / erhe_standard_variant.glsl).
vec3 evaluate_bxdf(Hit_surface surface, Material m, vec3 base_color, float roughness_x, float roughness_y, float metallic, vec3 L, vec3 V, vec3 N)
{
    if ((m.bxdf_model == 2u) || (m.bxdf_model == 4u)) { // anisotropic_brdf / anisotropic_engine_ready
        vec3 T = normalize(surface.tangent.xyz);
        vec3 B = normalize(cross(N, T)) * surface.tangent.w;
        return anisotropic_brdf(base_color, roughness_x, roughness_y, metallic, m.reflectance, L, V, T, B, N);
    }
    if (m.bxdf_model == 3u) { // anisotropic_slope
        vec3 T     = normalize(surface.tangent.xyz);
        vec3 B     = normalize(cross(N, T)) * surface.tangent.w;
        mat3 TBN_t = transpose(mat3(T, B, N));
        return slope_brdf(base_color, roughness_x, roughness_y, metallic, m.reflectance, TBN_t, L, V, T, B, N);
    }
    return isotropic_brdf(base_color, roughness_x, metallic, m.reflectance, L, V, N);
}

// Direct lighting at an opaque surface: ambient + emissive + per-light BxDF
// with ray traced shadows. Runtime light counts; the light buffer packs
// lights type-major (directional, spot, point).
vec3 shade_surface(Hit_surface surface, vec3 V)
{
    Material m          = material.materials[surface.material_index];
    vec3     base_color = surface_base_color(surface, m);

    if (m.bxdf_model == 0u) { // unlit
        return base_color;
    }

    vec3  N           = surface.shading_normal;
    float metallic    = m.metallic;
    float roughness_x = max(m.roughness.x, 1e-4);
    float roughness_y = max(m.roughness.y, 1e-4);

    vec3 metallic_roughness = sample_texture_lod0(
        m.metallic_roughness_texture,
        surface.texcoord,
        m.metallic_roughness_rotation_scale,
        m.metallic_roughness_offset
    ).rgb;
    metallic    = metallic * metallic_roughness.b;
    roughness_x = max(roughness_x * metallic_roughness.g, 1e-4);
    roughness_y = max(roughness_y * metallic_roughness.g, 1e-4);

    vec3 color = light_block.ambient_light.rgb * base_color;
    color += m.emissive.rgb;

    uint light_offset = 0u;

    for (uint i = 0u; i < light_block.directional_light_count; ++i) {
        Light light   = light_block.lights[light_offset + i];
        vec3  L       = normalize(light.direction_and_outer_spot_cos.xyz);
        float N_dot_L = dot(N, L);
        if ((N_dot_L > 0.0) && light_visible(surface.position, surface.geometric_normal, L, c_t_far)) {
            color += light.radiance_and_range.rgb * evaluate_bxdf(surface, m, base_color, roughness_x, roughness_y, metallic, L, V, N);
        }
    }
    light_offset += light_block.directional_light_count;

    for (uint i = 0u; i < light_block.spot_light_count; ++i) {
        Light light          = light_block.lights[light_offset + i];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - surface.position;
        float light_distance = length(point_to_light);
        vec3  L              = point_to_light / light_distance;
        float N_dot_L        = dot(N, L);
        if (N_dot_L > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, light_distance);
            float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float attenuation       = range_attenuation * spot_attenuation;
            if ((attenuation > 0.0) && light_visible(surface.position, surface.geometric_normal, L, light_distance)) {
                color += attenuation * light.radiance_and_range.rgb * evaluate_bxdf(surface, m, base_color, roughness_x, roughness_y, metallic, L, V, N);
            }
        }
    }
    light_offset += light_block.spot_light_count;

    for (uint i = 0u; i < light_block.point_light_count; ++i) {
        Light light          = light_block.lights[light_offset + i];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - surface.position;
        float light_distance = length(point_to_light);
        vec3  L              = point_to_light / light_distance;
        float N_dot_L        = dot(N, L);
        if (N_dot_L > 0.0) {
            float attenuation = get_range_attenuation(light.radiance_and_range.w, light_distance);
            if ((attenuation > 0.0) && light_visible(surface.position, surface.geometric_normal, L, light_distance)) {
                color += attenuation * light.radiance_and_range.rgb * evaluate_bxdf(surface, m, base_color, roughness_x, roughness_y, metallic, L, V, N);
            }
        }
    }

    return color;
}


#endif // ERHE_RAY_HIT_GLSL
