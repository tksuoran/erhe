#define a_id a_custom_0

layout(location = 0) out flat int v_draw_id;

vec3 vec3_from_uint(uint i)
{
    uint r = (i >> 16u) & 0xffu;
    uint g = (i >>  8u) & 0xffu;
    uint b = (i >>  0u) & 0xffu;

    return vec3(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0);
}

void main()
{
    mat4 world_from_node   = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world   = camera.cameras[0].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position            = clip_from_world * position_in_world;
    v_draw_id              = gl_DrawID;

#if 0
    v_id                   = a_id.rgb + primitive.primitives[gl_DrawID].color.xyz;
#endif
}

