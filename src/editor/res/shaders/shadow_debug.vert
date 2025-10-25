//#include "erhe_texture.glsl"
vec2 get_texture_size(uvec2 texture_handle) {
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2D s_texture = sampler2D(texture_handle);
    return textureSize(s_texture, 0);
#else
    return textureSize(s_texture[texture_handle.x], 0);
#endif
}

out flat uint v_instance_id ;
out flat uint v_texel_corner;
out flat uint v_cell_x      ;
out flat uint v_cell_y      ;

out vec4 v_pos_world;
out vec2 v_texel_offset;
out vec2 v_position_in_light_texture;

void main()
{

#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2DArrayShadow s_shadow_no_compare = sampler2DArrayShadow(light_block.shadow_texture_no_compare);
#endif

    //  Texel corner offsets .
    //                       .
    //  1.0 c-----+-----d    .
    //      |\          |    .
    //      |  \  |     |    .
    //      |    \      |    .
    //  0.5 +  -  +  -  +    .
    //      |      \    |    .
    //      |     |  \  |    .
    //      |          \|    .
    //  0.0 a-----+-----b    .
    //                       .
    //     0.0   0.5   1.0   .
    const vec2 texel_offsets[6] = vec2[6](
        vec2( 0.0,  0.0), // a
        vec2( 1.0,  0.0), // b
        vec2( 0.0,  1.0), // c
        vec2( 0.0,  1.0), // c
        vec2( 1.0,  0.0), // b
        vec2( 1.0,  1.0)  // d
    );

    uint  light_index = 0;
    Light light       = light_block.lights[light_index];

    //vec2 shadowmap_resolution = get_texture_size(light_block.shadow_texture_no_compare);
    vec2 shadowmap_resolution = vec2(256.0, 256.0);
    uint resolution_x = 256; //uint(shadowmap_resolution.x);
    uint instance_id  = gl_VertexID / 6;
    uint texel_corner = gl_VertexID % 6;
    uint cell_x       = instance_id % resolution_x;
    uint cell_y       = instance_id / resolution_x;
    vec2 texel_offset = texel_offsets[texel_corner];
    vec2 position_in_light_texture = vec2(
        float(cell_x + texel_offset.x) / shadowmap_resolution.x,
        float(cell_y + texel_offset.y) / shadowmap_resolution.y
    );
    float light_layer   = float(light_index);
    float lod           = 0.0;

    float shadow_sample = textureLod(s_shadow_no_compare, vec3(position_in_light_texture, light_layer), lod).x;
    v_pos_world = light.world_from_texture * vec4(position_in_light_texture, shadow_sample, 1.0);
    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    gl_Position = clip_from_world * v_pos_world;

    v_instance_id  = instance_id ;
    v_texel_corner = texel_corner;
    v_cell_x       = cell_x      ;
    v_cell_y       = cell_y      ;

    v_texel_offset              = texel_offset;
    v_position_in_light_texture = position_in_light_texture;

}
