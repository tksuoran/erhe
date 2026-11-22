#ifndef ERHE_DETAIL_BLEND_GLSL
#define ERHE_DETAIL_BLEND_GLSL

// doc/erhe/shader_variants.md "Detail blend". erhe::math::detail_blend() is
// the CPU twin of erhe_detail_blend(): keep the two in the same arithmetic.

// (h - 1 + w) / s + w is doc/erhe/math.md's form: w = 0 and w = 1 give exactly 0 and 1.
float erhe_detail_blend(float w, float h, float influence, float width, float pixel_width)
{
    float s = max(max(width, pixel_width), 1.0e-4);
    float d = clamp(((h - 1.0 + w) / s) + w, 0.0, 1.0);
    return w + ((d - w) * influence);
}

// An integer hash of a lattice point, in [0, 1).
float erhe_detail_blend_hash(uvec3 v)
{
    uint h = (v.x * 0x8da6b343u) ^ (v.y * 0xd8163841u) ^ (v.z * 0xcb1ab31fu);
    h ^= h >> 16u;
    h *= 0x7feb352du;
    h ^= h >> 15u;
    h *= 0x846ca68bu;
    h ^= h >> 16u;
    return float(h >> 8u) * (1.0 / 16777216.0);
}

// 3D value noise in [0, 1): the hash at the eight lattice corners of the
// cell, blended with a smoothstep weight. One evaluation, no texture read.
float erhe_surface_noise(vec3 p)
{
    vec3  cell = floor(p);
    vec3  f    = p - cell;
    vec3  u    = f * f * (3.0 - (2.0 * f));
    uvec3 c    = uvec3(ivec3(cell));
    float n000 = erhe_detail_blend_hash(c);
    float n100 = erhe_detail_blend_hash(c + uvec3(1u, 0u, 0u));
    float n010 = erhe_detail_blend_hash(c + uvec3(0u, 1u, 0u));
    float n110 = erhe_detail_blend_hash(c + uvec3(1u, 1u, 0u));
    float n001 = erhe_detail_blend_hash(c + uvec3(0u, 0u, 1u));
    float n101 = erhe_detail_blend_hash(c + uvec3(1u, 0u, 1u));
    float n011 = erhe_detail_blend_hash(c + uvec3(0u, 1u, 1u));
    float n111 = erhe_detail_blend_hash(c + uvec3(1u, 1u, 1u));
    float x00  = mix(n000, n100, u.x);
    float x10  = mix(n010, n110, u.x);
    float x01  = mix(n001, n101, u.x);
    float x11  = mix(n011, n111, u.x);
    return mix(mix(x00, x10, u.y), mix(x01, x11, u.y), u.z);
}

#endif // ERHE_DETAIL_BLEND_GLSL
