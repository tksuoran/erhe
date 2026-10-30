#ifndef ERHE_PVS_LAYER_GLSL
#define ERHE_PVS_LAYER_GLSL

// The depth layer function of the potentially visible set bake's layered
// buffer (doc/plans/pvs.md "Bake" 3): layer k of layer_count holds the
// linear view depths in [z_near * r^k, z_near * r^(k+1)), r = (z_far /
// z_near)^(1 / layer_count), so
//
//     layer(z) = floor(layer_count * log(z / z_near) / log(z_far / z_near))
//
// clamped to [0, layer_count - 1]. The caller passes only depths in
// [z_near, z_far]. erhe::scene_renderer::pvs_layer_of_depth() (pvs_layers.hpp)
// is the C++ twin: the same float operations in the same order.
uint erhe_pvs_layer_of_depth(float z, float z_near, float z_far, uint layer_count)
{
    float t     = log(z / z_near) / log(z_far / z_near);
    float layer = floor(float(layer_count) * t);
    return uint(clamp(layer, 0.0, float(layer_count - 1u)));
}

#endif // ERHE_PVS_LAYER_GLSL
