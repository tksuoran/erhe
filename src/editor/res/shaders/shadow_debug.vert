layout(location = 0) out float v_color;

void main()
{
#if defined(ERHE_HAS_ARB_BINDLESS_TEXTURE)
    sampler2DArray s_shadow_no_compare = sampler2DArray(light_block.shadow_texture_no_compare);
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
    const vec2 corner_texel_offsets[6] = vec2[6](
        vec2( 0.0,  0.0), // a
        vec2( 1.0,  0.0), // b
        vec2( 0.0,  1.0), // c
        vec2( 0.0,  1.0), // c
        vec2( 1.0,  0.0), // b
        vec2( 1.0,  1.0)  // d
    );

    uint  light_index = 0;
    Light light       = light_block.lights[light_index];

    vec2 shadowmap_resolution = textureSize(s_shadow_no_compare, 0).xy;
    uint resolution_x         = uint(shadowmap_resolution.x);
    uint instance_id          = gl_VertexID / 6;
    uint texel_corner_index   = gl_VertexID % 6;
    uint cell_x               = instance_id % resolution_x;
    uint cell_y               = instance_id / resolution_x;
    vec2 center_texel_offset  = vec2(0.5, 0.5);
    vec2 corner_texel_offset  = corner_texel_offsets[texel_corner_index];
    vec2 center_position_in_light_texture = vec2(
        float(cell_x + center_texel_offset.x) / shadowmap_resolution.x,
        float(cell_y + center_texel_offset.y) / shadowmap_resolution.y
    );
    vec2 corner_position_in_light_texture = vec2(
        float(cell_x + corner_texel_offset.x) / shadowmap_resolution.x,
        float(cell_y + corner_texel_offset.y) / shadowmap_resolution.y
    );
    float light_layer     = float(light_index);
    float lod             = 0.0;
    float shadow_sample   = textureLod(s_shadow_no_compare, vec3(center_position_in_light_texture, light_layer), lod).x;
    vec4  pos_world       = light.world_from_texture * vec4(corner_position_in_light_texture, shadow_sample, 1.0);
    mat4  clip_from_world = camera.cameras[0].clip_from_world;
    v_color     = float((cell_x ^ cell_y) & 1u);
    gl_Position = clip_from_world * pos_world;
}
