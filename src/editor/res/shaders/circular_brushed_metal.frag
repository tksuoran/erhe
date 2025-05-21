#include "erhe_bxdf.glsl"
#include "erhe_light.glsl"
#include "erhe_texture.glsl"

layout(location = 0) in vec2      v_texcoord;
layout(location = 1) in vec4      v_position;
layout(location = 2) in vec4      v_color;
layout(location = 3) in vec2      v_aniso_control;
layout(location = 4) in mat3      v_TBN;
layout(location = 7) in flat uint v_material_index;

void main() {
    const float bayer_matrix[256] = float[256](
          0.0 / 255.0, 128.0 / 255.0,  32.0 / 255.0, 160.0 / 255.0,   8.0 / 255.0, 136.0 / 255.0,  40.0 / 255.0, 168.0 / 255.0,   2.0 / 255.0, 130.0 / 255.0,  34.0 / 255.0, 162.0 / 255.0,  10.0 / 255.0, 138.0 / 255.0,  42.0 / 255.0, 170.0 / 255.0,
        192.0 / 255.0,  64.0 / 255.0, 224.0 / 255.0,  96.0 / 255.0, 200.0 / 255.0,  72.0 / 255.0, 232.0 / 255.0, 104.0 / 255.0, 194.0 / 255.0,  66.0 / 255.0, 226.0 / 255.0,  98.0 / 255.0, 202.0 / 255.0,  74.0 / 255.0, 234.0 / 255.0, 106.0 / 255.0,
         48.0 / 255.0, 176.0 / 255.0,  16.0 / 255.0, 144.0 / 255.0,  56.0 / 255.0, 184.0 / 255.0,  24.0 / 255.0, 152.0 / 255.0,  50.0 / 255.0, 178.0 / 255.0,  18.0 / 255.0, 146.0 / 255.0,  58.0 / 255.0, 186.0 / 255.0,  26.0 / 255.0, 154.0 / 255.0,
        240.0 / 255.0, 112.0 / 255.0, 208.0 / 255.0,  80.0 / 255.0, 248.0 / 255.0, 120.0 / 255.0, 216.0 / 255.0,  88.0 / 255.0, 242.0 / 255.0, 114.0 / 255.0, 210.0 / 255.0,  82.0 / 255.0, 250.0 / 255.0, 122.0 / 255.0, 218.0 / 255.0,  90.0 / 255.0,
         12.0 / 255.0, 140.0 / 255.0,  44.0 / 255.0, 172.0 / 255.0,   4.0 / 255.0, 132.0 / 255.0,  36.0 / 255.0, 164.0 / 255.0,  14.0 / 255.0, 142.0 / 255.0,  46.0 / 255.0, 174.0 / 255.0,   6.0 / 255.0, 134.0 / 255.0,  38.0 / 255.0, 166.0 / 255.0,
        204.0 / 255.0,  76.0 / 255.0, 236.0 / 255.0, 108.0 / 255.0, 196.0 / 255.0,  68.0 / 255.0, 228.0 / 255.0, 100.0 / 255.0, 206.0 / 255.0,  78.0 / 255.0, 238.0 / 255.0, 110.0 / 255.0, 198.0 / 255.0,  70.0 / 255.0, 230.0 / 255.0, 102.0 / 255.0,
         60.0 / 255.0, 188.0 / 255.0,  28.0 / 255.0, 156.0 / 255.0,  52.0 / 255.0, 180.0 / 255.0,  20.0 / 255.0, 148.0 / 255.0,  62.0 / 255.0, 190.0 / 255.0,  30.0 / 255.0, 158.0 / 255.0,  54.0 / 255.0, 182.0 / 255.0,  22.0 / 255.0, 150.0 / 255.0,
        252.0 / 255.0, 124.0 / 255.0, 220.0 / 255.0,  92.0 / 255.0, 244.0 / 255.0, 116.0 / 255.0, 212.0 / 255.0,  84.0 / 255.0, 254.0 / 255.0, 126.0 / 255.0, 222.0 / 255.0,  94.0 / 255.0, 246.0 / 255.0, 118.0 / 255.0, 214.0 / 255.0,  86.0 / 255.0,
          3.0 / 255.0, 131.0 / 255.0,  35.0 / 255.0, 163.0 / 255.0,  11.0 / 255.0, 139.0 / 255.0,  43.0 / 255.0, 171.0 / 255.0,   1.0 / 255.0, 129.0 / 255.0,  33.0 / 255.0, 161.0 / 255.0,   9.0 / 255.0, 137.0 / 255.0,  41.0 / 255.0, 169.0 / 255.0,
        195.0 / 255.0,  67.0 / 255.0, 227.0 / 255.0,  99.0 / 255.0, 203.0 / 255.0,  75.0 / 255.0, 235.0 / 255.0, 107.0 / 255.0, 193.0 / 255.0,  65.0 / 255.0, 225.0 / 255.0,  97.0 / 255.0, 201.0 / 255.0,  73.0 / 255.0, 233.0 / 255.0, 105.0 / 255.0,
         51.0 / 255.0, 179.0 / 255.0,  19.0 / 255.0, 147.0 / 255.0,  59.0 / 255.0, 187.0 / 255.0,  27.0 / 255.0, 155.0 / 255.0,  49.0 / 255.0, 177.0 / 255.0,  17.0 / 255.0, 145.0 / 255.0,  57.0 / 255.0, 185.0 / 255.0,  25.0 / 255.0, 153.0 / 255.0,
        243.0 / 255.0, 115.0 / 255.0, 211.0 / 255.0,  83.0 / 255.0, 251.0 / 255.0, 123.0 / 255.0, 219.0 / 255.0,  91.0 / 255.0, 241.0 / 255.0, 113.0 / 255.0, 209.0 / 255.0,  81.0 / 255.0, 249.0 / 255.0, 121.0 / 255.0, 217.0 / 255.0,  89.0 / 255.0,
         15.0 / 255.0, 143.0 / 255.0,  47.0 / 255.0, 175.0 / 255.0,   7.0 / 255.0, 135.0 / 255.0,  39.0 / 255.0, 167.0 / 255.0,  13.0 / 255.0, 141.0 / 255.0,  45.0 / 255.0, 173.0 / 255.0,   5.0 / 255.0, 133.0 / 255.0,  37.0 / 255.0, 165.0 / 255.0,
        207.0 / 255.0,  79.0 / 255.0, 239.0 / 255.0, 111.0 / 255.0, 199.0 / 255.0,  71.0 / 255.0, 231.0 / 255.0, 103.0 / 255.0, 205.0 / 255.0,  77.0 / 255.0, 237.0 / 255.0, 109.0 / 255.0, 197.0 / 255.0,  69.0 / 255.0, 229.0 / 255.0, 101.0 / 255.0,
         63.0 / 255.0, 191.0 / 255.0,  31.0 / 255.0, 159.0 / 255.0,  55.0 / 255.0, 183.0 / 255.0,  23.0 / 255.0, 151.0 / 255.0,  61.0 / 255.0, 189.0 / 255.0,  29.0 / 255.0, 157.0 / 255.0,  53.0 / 255.0, 181.0 / 255.0,  21.0 / 255.0, 149.0 / 255.0,
        255.0 / 255.0, 127.0 / 255.0, 223.0 / 255.0,  95.0 / 255.0, 247.0 / 255.0, 119.0 / 255.0, 215.0 / 255.0,  87.0 / 255.0, 253.0 / 255.0, 125.0 / 255.0, 221.0 / 255.0,  93.0 / 255.0, 245.0 / 255.0, 117.0 / 255.0, 213.0 / 255.0,  85.0 / 255.0
    );

    const uint even_bits[32] = uint[32](
        0u, 1u, 0u, 1u, 1u, 2u, 1u, 2u,
        0u, 1u, 0u, 1u, 1u, 2u, 1u, 2u,
        1u, 2u, 1u, 2u, 2u, 3u, 2u, 3u,
        1u, 2u, 1u, 2u, 2u, 3u, 2u, 3u
    );

    const uint odd_bits[32] = uint[32](
        0u, 0u, 1u, 1u, 0u, 0u, 1u, 1u,
        1u, 1u, 2u, 2u, 1u, 1u, 2u, 2u,
        0u, 0u, 1u, 1u, 0u, 0u, 1u, 1u,
        1u, 1u, 2u, 2u, 1u, 1u, 2u, 2u
    );
    const ivec2 dither_pos   = ivec2(
        uint(gl_FragCoord.x) + odd_bits[gl_SampleID & 31],
        uint(gl_FragCoord.y) + even_bits[gl_SampleID & 31]
    ) % 16;
    const int   dither_index = (dither_pos.y * 16 + dither_pos.x + gl_SampleID) & 255;
    const float dither_value = bayer_matrix[dither_index];

    Material material = material.materials[v_material_index];
    float opacity = material.opacity;
    if (opacity < dither_value) {
        discard;
    }

    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  V  = normalize(view_position_in_world - v_position.xyz);
    vec3  T0 = normalize(v_TBN[0]); // Geometry tangent from vertex attribute
    vec3  B0 = normalize(v_TBN[1]); // Geometry bitangent from vertex attribute
    vec3  N  = normalize(v_TBN[2]);
    float N_dot_V = clamped_dot(N, V);

    uvec2 base_color_texture         = material.base_color_texture;
    uvec2 metallic_roughness_texture = material.metallic_roughness_texture;
    vec3  base_color                 = v_color.rgb * material.base_color.rgb * sample_texture(base_color_texture, v_texcoord).rgb;

    // Generating circular anisotropy direction from texcoord
    float circular_anisotropy_magnitude = pow(length(v_texcoord) * 8.0, 0.25);
    vec2  T_circular                    = (circular_anisotropy_magnitude > 0.0) ? normalize(v_texcoord) : vec2(0.0, 0.0);
    // X is used to modulate anisotropy level:
    //   0.0 -- Anisotropic
    //   1.0 -- Isotropic when approaching texcoord (0, 0)
    // Y is used for tangent space selection/control:
    //   0.0 -- Use geometry T and B (from vertex attribute
    //   1.0 -- Use T and B derived from texcoord
    // GLSL mix(a, b, w) = a * (1 - w) + y * w   w == 0 -> a, w == 1 -> b
    float anisotropy_strength = mix(
        1.0,
        min(1.0, circular_anisotropy_magnitude),
        v_aniso_control.y
    ) * v_aniso_control.x;
    // Mix tangent space geometric .. texcoord generated
    vec3  T                   = circular_anisotropy_magnitude > 0.0 ? mix(T0, T_circular.x * T0 + T_circular.y * B0, v_aniso_control.y) : T0;
    vec3  B                   = circular_anisotropy_magnitude > 0.0 ? mix(B0, T_circular.y * T0 - T_circular.x * B0, v_aniso_control.y) : B0;
    float isotropic_roughness = 0.5 * material.roughness.x + 0.5 * material.roughness.y;
    // Mix roughness based on anisotropy_strength
    float roughness_x         = mix(isotropic_roughness, material.roughness.x, anisotropy_strength);
    float roughness_y         = mix(isotropic_roughness, material.roughness.y, anisotropy_strength);

    uint directional_light_count  = light_block.directional_light_count;
    uint spot_light_count         = light_block.spot_light_count;
    uint point_light_count        = light_block.point_light_count;
    uint directional_light_offset = 0;
    uint spot_light_offset        = directional_light_count;
    uint point_light_offset       = spot_light_offset + spot_light_count;
    vec3 color = vec3(0);
    color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
    color += material.emissive.rgb;

    for (uint i = 0; i < directional_light_count; ++i) {
        uint  light_index    = directional_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
        vec3  L              = normalize(point_to_light);   // Direction from surface point to light
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
            color += intensity * anisotropic_brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < spot_light_count; ++i) {
        uint  light_index    = spot_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * anisotropic_brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    for (uint i = 0; i < point_light_count; ++i) {
        uint  light_index    = point_light_offset + i;
        Light light          = light_block.lights[light_index];
        vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  L              = normalize(point_to_light);
        float N_dot_L        = clamped_dot(N, L);
        if (N_dot_L > 0.0 || N_dot_V > 0.0) {
            float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
            float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
            vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
            color += intensity * anisotropic_brdf(
                base_color,
                roughness_x,
                roughness_y,
                material.metallic,
                material.reflectance,
                L,
                V,
                T,
                B,
                N
            );
        }
    }

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure * material.opacity;
    //out_color.a = material.opacity;
    out_color.a = 1.0;
}
