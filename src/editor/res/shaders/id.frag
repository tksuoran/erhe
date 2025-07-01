layout(location = 0) in flat int v_draw_id;

vec3 vec3_from_uint(uint i)
{
    uint r = (i >> 16u) & 0xffu;
    uint g = (i >>  8u) & 0xffu;
    uint b = (i >>  0u) & 0xffu;

    return vec3(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0);
}

void main(void)
{
    uint triangle_id = uint(gl_PrimitiveID);
    vec3 id_rgb      = vec3_from_uint(triangle_id);
    vec3 id          = id_rgb + primitive.primitives[v_draw_id].color.xyz;
    // Debug: Overflow detection
    // if (any(greaterThan(id, vec3(1.0)))) {
    //     id = vec3(1.0, 1.0, 1.0);
    // }
    out_color = vec4(id, 1.0);
}
