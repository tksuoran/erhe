#include "erhe_camera_view.glsl"
#define a_id a_custom_0

layout(location = 0) flat out int v_draw_id;
// Reading gl_PrimitiveID in a fragment shader pulls in the SPIR-V Geometry
// capability (and on Metal isn't supported at all). The id renderer always
// draws triangle lists, so the vertex shader writes a per-vertex triangle
// index instead.
layout(location = 1) flat out int v_primitive_id;

vec3 vec3_from_uint(uint i)
{
    uint r = (i >> 16u) & 0xffu;
    uint g = (i >>  8u) & 0xffu;
    uint b = (i >>  0u) & 0xffu;

    return vec3(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0);
}

void main()
{
    mat4 world_from_node   = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    mat4 clip_from_world   = camera.cameras[c_view_index].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position            = clip_from_world * position_in_world;
    v_draw_id              = ERHE_DRAW_ID;
    v_primitive_id         = gl_VertexID / 3;

#if 0
    v_id                   = a_id.rgb + primitive.primitives[ERHE_DRAW_ID].color.xyz;
#endif
}

