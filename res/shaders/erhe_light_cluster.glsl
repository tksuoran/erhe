#ifndef ERHE_LIGHT_CLUSTER_GLSL
#define ERHE_LIGHT_CLUSTER_GLSL

#include "erhe_camera_view.glsl"

// Clustered light shading (doc/plans/clustered_lights.md C2, C5). The grid is
// the light block's cluster_* header (erhe::scene_renderer::Light_cluster_grid);
// light_cluster.data[] holds three uints per cluster (list offset, spot counts,
// point counts) and the lists, two 16-bit light slots per uint
// (erhe_scene_renderer/light_cluster.hpp).

// The cluster of the fragment at view_relative_position (relative to the
// pass's view origin). Positions outside the cluster frustum clamp into the
// boundary clusters, whose lists cover the outside (C2). A position behind the
// cluster camera (clip w <= 0) has view depth <= 0, so it lands in slice 0,
// whose tiles are open sideways (Light_cluster_binner); any tile serves.
uint erhe_light_cluster_index(vec3 view_relative_position)
{
    vec3  p      = view_relative_position + (camera.cameras[c_view_index].view_origin.xyz - light_block.cluster_view_origin.xyz);
    vec4  clip   = light_block.cluster_clip_from_cluster_relative * vec4(p, 1.0);
    uvec4 counts = light_block.cluster_counts;
    vec2  ndc    = (clip.w > 0.0) ? (clip.xy / clip.w) : vec2(0.0);
    vec2  tile_f  = clamp(floor(((ndc * 0.5) + vec2(0.5)) * vec2(counts.xy)), vec2(0.0), vec2(counts.xy) - vec2(1.0));
    float depth   = -dot(p, light_block.cluster_view_axis.xyz);
    float slice_f = clamp(
        floor((log2(max(depth, light_block.cluster_slices.z)) * light_block.cluster_slices.x) + light_block.cluster_slices.y),
        0.0,
        float(counts.z) - 1.0
    );
    return (((uint(slice_f) * counts.y) + uint(tile_f.y)) * counts.x) + uint(tile_f.x);
}

// The index in light_cluster.data[] of the cluster's header: the list offset,
// then the spot counts, then the point counts. ERHE_LIGHT_CLUSTER_HEADER_UINT_COUNT
// is defined by Program_interface from c_light_cluster_header_uint_count.
uint erhe_light_cluster_header(uint cluster)
{
    return ERHE_LIGHT_CLUSTER_HEADER_UINT_COUNT * cluster;
}

// Entry list_index of the light list at list_offset: a light array slot.
uint erhe_light_cluster_slot(uint list_offset, uint list_index)
{
    uint packed = light_cluster.data[list_offset + (list_index >> 1u)];
    return ((list_index & 1u) == 0u) ? (packed & 0xffffu) : (packed >> 16u);
}

#endif // ERHE_LIGHT_CLUSTER_GLSL
