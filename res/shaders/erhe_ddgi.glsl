#ifndef ERHE_DDGI_GLSL
#define ERHE_DDGI_GLSL

// DDGI irradiance sampling (doc/ddgi-plan.md phase 6).
//
// Reads the probe volume the Ddgi_renderer maintains: an octahedral
// irradiance atlas, an octahedral mean / mean-squared distance atlas for the
// Chebyshev visibility test, and one texel per probe carrying the relocation
// offset and the active flag. The grid itself rides in light_block
// (ddgi_grid_origin / ddgi_grid_spacing / ddgi_counts / ddgi_texels /
// ddgi_params), with ddgi_counts.w as the "volume exists" gate.
//
// The eight probes of the cell containing the (biased) shading point are
// combined with three weights, following Majercik et al. 2019:
//  - trilinear position weight, so the field is continuous,
//  - a smooth backface weight, so probes behind the surface fade out
//    instead of popping,
//  - a Chebyshev visibility weight from the distance atlas, which is what
//    stops a probe on the far side of a wall from lighting this surface.

// Octahedral encode: unit sphere -> [-1,1]^2 square (Cigolle et al. 2014).
vec2 ddgi_octahedral_encode(vec3 direction)
{
    vec3 n = direction / (abs(direction.x) + abs(direction.y) + abs(direction.z));
    vec2 f = n.xy;
    if (n.z < 0.0) {
        f = (1.0 - abs(n.yx)) * vec2((n.x >= 0.0) ? 1.0 : -1.0, (n.y >= 0.0) ? 1.0 : -1.0);
    }
    return f;
}

// Atlas UV of a probe's octahedral texel for the given direction. The tile
// carries a 1-texel border on every side, so the interior is inset by one
// texel and the sampled range never reaches a neighbouring probe.
vec2 ddgi_probe_uv(ivec3 probe_coords, vec3 direction, int interior_texels, vec2 atlas_size)
{
    int   tile_size   = interior_texels + 2;
    ivec2 tile_index  = ivec2(probe_coords.x + int(light_block.ddgi_counts.x) * probe_coords.z, probe_coords.y);
    vec2  tile_origin = vec2(tile_index * tile_size);
    vec2  local       = (ddgi_octahedral_encode(direction) * 0.5 + 0.5) * float(interior_texels) + vec2(1.0);
    return (tile_origin + local) / atlas_size;
}

vec3 ddgi_probe_position(ivec3 probe_coords)
{
    vec3  base  = light_block.ddgi_grid_origin.xyz + vec3(probe_coords) * light_block.ddgi_grid_spacing.xyz;
    ivec2 texel = ivec2(probe_coords.x + int(light_block.ddgi_counts.x) * probe_coords.z, probe_coords.y);
    return base + texelFetch(s_ddgi_probe_data, texel, 0).xyz;
}

float ddgi_probe_state(ivec3 probe_coords)
{
    ivec2 texel = ivec2(probe_coords.x + int(light_block.ddgi_counts.x) * probe_coords.z, probe_coords.y);
    return texelFetch(s_ddgi_probe_data, texel, 0).w;
}

// Indirect diffuse irradiance arriving at (world_position, normal), already
// scaled by the configured intensity. Returns the flat ambient term when no
// volume is active, so callers can use it unconditionally.
vec3 ddgi_sample_irradiance(vec3 world_position, vec3 normal, vec3 view_direction)
{
    if (light_block.ddgi_counts.w == 0u) {
        return light_block.ambient_light.rgb;
    }

    ivec3 counts            = ivec3(light_block.ddgi_counts.xyz);
    vec3  spacing           = light_block.ddgi_grid_spacing.xyz;
    int   irradiance_texels = int(light_block.ddgi_texels.x);
    int   distance_texels   = int(light_block.ddgi_texels.y);
    float normal_bias       = light_block.ddgi_params.x;
    float view_bias         = light_block.ddgi_params.y;
    float intensity         = light_block.ddgi_params.w;

    vec2 irradiance_atlas_size = vec2(textureSize(s_ddgi_irradiance, 0));
    vec2 distance_atlas_size   = vec2(textureSize(s_ddgi_distance,   0));

    // Surface bias: pull the sample point off the surface so it is not
    // exactly on the visibility boundary the probes recorded.
    vec3 biased_position = world_position + normal * normal_bias + view_direction * view_bias;

    vec3  grid_position = (biased_position - light_block.ddgi_grid_origin.xyz) / spacing;
    vec3  base_float    = floor(grid_position);
    vec3  alpha         = clamp(grid_position - base_float, vec3(0.0), vec3(1.0));
    ivec3 base_coords   = clamp(ivec3(base_float), ivec3(0), counts - ivec3(2));

    vec3  irradiance_sum = vec3(0.0);
    float weight_sum     = 0.0;

    for (int corner = 0; corner < 8; ++corner) {
        ivec3 offset       = ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
        ivec3 probe_coords = clamp(base_coords + offset, ivec3(0), counts - ivec3(1));

        // Classification: probes enclosed in geometry contribute nothing.
        if (ddgi_probe_state(probe_coords) < 0.5) {
            continue;
        }

        vec3  probe_position = ddgi_probe_position(probe_coords);
        vec3  to_probe       = probe_position - biased_position;
        float probe_distance = length(to_probe);
        vec3  probe_direction = (probe_distance > 1.0e-6) ? (to_probe / probe_distance) : normal;

        // Trilinear weight of this corner.
        vec3  trilinear = mix(vec3(1.0) - alpha, alpha, vec3(offset));
        float weight    = trilinear.x * trilinear.y * trilinear.z;

        // Smooth backface weight: probes behind the surface fade out rather
        // than switching off, which would show as a hard seam.
        float n_dot_d = dot(normal, probe_direction);
        weight *= (n_dot_d * 0.5 + 0.5) * (n_dot_d * 0.5 + 0.5) + 0.2;

        // Chebyshev visibility: how likely is this probe to actually see the
        // shading point, given the mean and variance of the distances it
        // recorded toward it.
        vec2  distance_uv = ddgi_probe_uv(probe_coords, -probe_direction, distance_texels, distance_atlas_size);
        vec2  moments     = texture(s_ddgi_distance, distance_uv).rg;
        float mean        = moments.x;
        float variance    = abs(mean * mean - moments.y);
        if (probe_distance > mean) {
            float t                 = probe_distance - mean;
            float chebyshev         = variance / (variance + t * t);
            // The cube sharpens the falloff; without it the test leaves a
            // visible halo around occluders.
            weight *= max(chebyshev * chebyshev * chebyshev, 0.0);
        }

        // A tiny floor keeps the sum from collapsing when every probe is
        // marginally occluded, which would otherwise leave black patches.
        weight = max(weight, 1.0e-4);

        vec2 irradiance_uv = ddgi_probe_uv(probe_coords, normal, irradiance_texels, irradiance_atlas_size);
        irradiance_sum += texture(s_ddgi_irradiance, irradiance_uv).rgb * weight;
        weight_sum     += weight;
    }

    if (weight_sum <= 0.0) {
        return light_block.ambient_light.rgb;
    }
    return (irradiance_sum / weight_sum) * intensity;
}

#endif // ERHE_DDGI_GLSL
