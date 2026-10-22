#ifndef ERHE_LIGHT_RECORD_GLSL
#define ERHE_LIGHT_RECORD_GLSL

// The shadow record index of a shadow-mapped light slot
// (light_shadowed_array.lights[], light_shadow_world_array.transforms[]): the
// slot less the unshadowed slots before it. Slots are type major with the
// shadow-mapped lights first within a type (doc/erhe/shader_variants.md
// "Light array layout invariant"), so those are the unshadowed directional
// slots for a spot light and the unshadowed directional and spot slots for a
// point light. Equal to the 2D shadow map layer for directional and spot
// lights and to the 2D layer count plus the cube index for point lights
// (Light_buffer get_shadow_record_index()).
uint erhe_light_shadow_record(uint slot) {
    uint directional_unshadowed = light_block.directional_light_count - light_block.directional_shadow_count;
    uint spot_unshadowed        = light_block.spot_light_count        - light_block.spot_shadow_count;
    if (slot < light_block.directional_light_count) {
        return slot;
    }
    if (slot < (light_block.directional_light_count + light_block.spot_light_count)) {
        return slot - directional_unshadowed;
    }
    return slot - directional_unshadowed - spot_unshadowed;
}

#endif // ERHE_LIGHT_RECORD_GLSL
