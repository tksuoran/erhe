#include "erhe_camera_view.glsl"

// Fullscreen-triangle vertex stage for the physically-based sky. Places the
// triangle at the far plane (depth convention aware) so it composites behind
// the scene, and reconstructs the world-space view ray from the camera UBO
// (same mechanism as the gradient sky.vert). The camera block is injected by
// the bind group layout; multiview indexing is via c_view_index.

layout(location = 0) out highp vec3 v_ray_direction;

void main()
{
    // Fullscreen triangle (same layout as sky.vert).
    const vec4 positions[3] = vec4[3](
        vec4(-1.0, -1.0, 0.0, 1.0),
        vec4( 3.0, -1.0, 0.0, 1.0),
        vec4(-1.0,  3.0, 0.0, 1.0)
    );

    // Far-plane clip z: 0 for reverse-Z, 1 for forward-Z.
    float far_clip_z = (camera.cameras[c_view_index].clip_depth_direction < 0.0) ? 0.0 : 1.0;
    vec4  clip_position = vec4(positions[gl_VertexID].xy, far_clip_z, 1.0);
    gl_Position = clip_position;

    highp mat4 world_from_clip = camera.cameras[c_view_index].world_from_clip;
    highp vec4 world_far       = world_from_clip * clip_position;
    highp vec3 world_far_pos   = world_far.xyz / world_far.w;
    highp vec3 camera_pos      = vec3(
        camera.cameras[c_view_index].world_from_node[3][0],
        camera.cameras[c_view_index].world_from_node[3][1],
        camera.cameras[c_view_index].world_from_node[3][2]
    );
    // Direction the camera looks through this pixel (frag normalizes).
    v_ray_direction = world_far_pos - camera_pos;
}
