#include "erhe_camera_view.glsl"
#ifdef ERHE_USE_SKINNING
#include "erhe_skinning.glsl"
#endif

layout(location = 0) out float v_line_width;
layout(location = 1) out vec4  v_color;
layout(location = 2) out vec4  v_start_end;

// Full line width in framebuffer pixels. The ribbon offsets go through
// ndc_from_screen_direction(), which maps it to half of it on each side.
float get_line_width(vec4 position, float thickness, vec4 fov, vec4 viewport, float pixel_scale)
{
    // Negative thickness: a constant screen-space line -thickness logical
    // pixels wide, scaled to framebuffer pixels by the view's pixel scale.
    if (thickness < 0.0) {
        return -thickness * pixel_scale;
    }
    // Positive thickness: distance-scaled width (wide_lines.vert).
    float fov_width        = fov[1] - fov[0];
    float viewport_width   = viewport[2];
    float max_size         = 4.0 * thickness;
    float scaled_thickness = max(max_size / position.w, 1.0);
    return (1.0 / 1024.0) * viewport_width * scaled_thickness / fov_width;
}

bool clip_line(
    in  vec4  v0,
    in  vec4  v1,
    out vec4  clipped_v0,
    out vec4  clipped_v1
)
{
    vec4  l      = v1 - v0;
    float denom4 =  l.z + l.w; float num4 = -v0.z - v0.w; float t4 = num4 / denom4;
    float denom5 = -l.z + l.w; float num5 =  v0.z - v0.w; float t5 = num5 / denom5;
    float tmin   = 0.0;
    float tmax   = 1.0;
    if (denom4 > 0.0) {
        if (t4 > tmax) return false;
        if (t4 > tmin) tmin = t4;
    } else if (denom4 < 0.0) {
        if (t4 < tmin) return false;
        if (t4 < tmax) tmax = t4;
    } else if (num4 > 0.0) {
        return false;
    }
    if (denom5 > 0.0) {
        if (t5 > tmax) return false;
        if (t5 > tmin) tmin = t5;
    } else if (denom5 < 0.0) {
        if (t5 < tmin) return false;
        if (t5 < tmax) tmax = t5;
    } else if (num5 > 0.0) {
        return false;
    }
    clipped_v0 = mix(v0, v1, tmin);
    clipped_v1 = mix(v0, v1, tmax);
    return true;
}

vec2 screen_from_ndc(vec2 ndc, vec2 vp_size)
{
    return (ndc * 0.5 + 0.5) * vp_size;
}

vec2 ndc_from_screen_direction(vec2 screen, vec2 vp_size)
{
    return screen / vp_size;
}

float plane_z(vec2 xy, vec3 ref_ndc, vec3 plane_n)
{
    if (abs(plane_n.z) < 1e-6) return ref_ndc.z;
    return ref_ndc.z - (plane_n.x * (xy.x - ref_ndc.x) + plane_n.y * (xy.y - ref_ndc.y)) / plane_n.z;
}

void main()
{
    // Edge from the vertex index, quad corner from the remainder. Corner
    // order a d b / b d c: two CCW triangles (doc/erhe/scene_renderer.md).
    uint edge   = uint(gl_VertexID) / 6u;
    uint corner = uint(gl_VertexID) % 6u;
    uint in_i0  = 2u * edge;
    uint in_i1  = in_i0 + 1u;

    // Endpoint 0 carries face A's normal, endpoint 1 face B's
    // (Build_context::build_edge_lines).
    vec3 local_position0 = edge_line_vertex_buffer.vertices[in_i0].position.xyz;
    vec3 local_position1 = edge_line_vertex_buffer.vertices[in_i1].position.xyz;
    vec3 local_normal_a  = edge_line_vertex_buffer.vertices[in_i0].normal.xyz;
    vec3 local_normal_b  = edge_line_vertex_buffer.vertices[in_i1].normal.xyz;

    mat4 clip_from_world = view.cameras[c_view_index].clip_from_world;

#ifdef ERHE_USE_SKINNING
    // Per-endpoint linear-blend skinning: an edge's endpoints often span
    // different bones.
    mat4 world_from_bind_0, world_from_bind_normal_0;
    mat4 world_from_bind_1, world_from_bind_normal_1;
    erhe_skin_matrices(
        view.base_joint_index,
        edge_line_joint_buffer.vertices[in_i0].joint_indices,
        edge_line_joint_buffer.vertices[in_i0].joint_weights,
        world_from_bind_0,
        world_from_bind_normal_0
    );
    erhe_skin_matrices(
        view.base_joint_index,
        edge_line_joint_buffer.vertices[in_i1].joint_indices,
        edge_line_joint_buffer.vertices[in_i1].joint_weights,
        world_from_bind_1,
        world_from_bind_normal_1
    );
    vec4 world_pos0     = world_from_bind_0 * vec4(local_position0, 1.0);
    vec4 world_pos1     = world_from_bind_1 * vec4(local_position1, 1.0);
    vec3 world_normal_a = normalize(vec3(world_from_bind_normal_0 * vec4(local_normal_a, 0.0)));
    vec3 world_normal_b = normalize(vec3(world_from_bind_normal_1 * vec4(local_normal_b, 0.0)));
#else
    // Inverse-transpose keeps the face normals right under non-uniform scale.
    mat4 world_from_node = view.world_from_node;
    vec4 world_pos0      = world_from_node * vec4(local_position0, 1.0);
    vec4 world_pos1      = world_from_node * vec4(local_position1, 1.0);
    mat3 normal_matrix   = mat3(transpose(inverse(world_from_node)));
    vec3 world_normal_a  = normalize(normal_matrix * local_normal_a);
    vec3 world_normal_b  = normalize(normal_matrix * local_normal_b);
#endif

    float line_thickness = view.line_color.w;
    vec4  fov            = view.cameras[c_view_index].fov;
    vec4  viewport       = view.cameras[c_view_index].viewport;
    float pixel_scale    = view.cameras[c_view_index].pixel_scale;
    vec2  vp_size        = viewport.zw;

    v_color = vec4(view.line_color.xyz, 1.0);

    vec3 world_normal = normalize(world_normal_a + world_normal_b);
    vec4 v0           = clip_from_world * world_pos0;
    vec4 v1           = clip_from_world * world_pos1;

    // Depth bias against z-fighting with the surface: 0.0005 * NdotV^2 in
    // the clip depth direction, none for back-facing endpoints.
    vec3  view_position_in_world = view.cameras[c_view_index].view_position_in_world.xyz;
    float NdotV0 = clamp(dot(world_normal_a, normalize(view_position_in_world - world_pos0.xyz)), 0.0, 1.0);
    float NdotV1 = clamp(dot(world_normal_b, normalize(view_position_in_world - world_pos1.xyz)), 0.0, 1.0);
    v0.z -= 0.0005 * NdotV0 * NdotV0 * view.clip_depth_direction;
    v1.z -= 0.0005 * NdotV1 * NdotV1 * view.clip_depth_direction;

    vec4 v0_clipped;
    vec4 v1_clipped;
    if (!clip_line(v0, v1, v0_clipped, v1_clipped)) {
        // Every corner of a clipped-away edge at one point: a zero-area quad.
        gl_Position  = vec4(0.0, 0.0, 0.0, 1.0);
        v_line_width = 0.0;
        v_start_end  = vec4(0.0);
        return;
    }

    float width0 = get_line_width(v0_clipped, line_thickness, fov, viewport, pixel_scale);
    float width1 = get_line_width(v1_clipped, line_thickness, fov, viewport, pixel_scale);

    vec3 v0_in_ndc = v0_clipped.xyz / v0_clipped.w;
    vec3 v1_in_ndc = v1_clipped.xyz / v1_clipped.w;

    // The NDC plane through the edge along the averaged face normal gives
    // every corner its depth.
    vec3  edge_dir_world = normalize(world_pos1.xyz - world_pos0.xyz);
    vec3  side_cross     = cross(world_normal, edge_dir_world);
    float side_cross_len = length(side_cross);
    vec3  side_dir_world = (side_cross_len > 1e-6)
        ? side_cross / side_cross_len
        : normalize(cross(edge_dir_world, (abs(edge_dir_world.x) < 0.9) ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0)));
    float edge_len       = length(world_pos1.xyz - world_pos0.xyz);
    vec4  clip_side      = clip_from_world * vec4(world_pos0.xyz + side_dir_world * edge_len, 1.0);
    vec3  ndc_side       = clip_side.xyz / clip_side.w;
    vec3  plane_n        = cross(v1_in_ndc - v0_in_ndc, ndc_side - v0_in_ndc);

    vec2 v0_in_screen   = screen_from_ndc(v0_in_ndc.xy, vp_size);
    vec2 v1_in_screen   = screen_from_ndc(v1_in_ndc.xy, vp_size);
    vec2 axis_in_screen = normalize(v1_in_screen - v0_in_screen);
    vec2 side_in_screen = vec2(-axis_in_screen.y, axis_in_screen.x);
    vec2 axis_in_ndc    = ndc_from_screen_direction(axis_in_screen, vp_size);
    vec2 side_in_ndc    = ndc_from_screen_direction(side_in_screen, vp_size);

    //  a - - - - - - - - - - b
    //  |  v0 - - - - - -> v1 |
    //  d - - - - - - - - - - c
    // Corners: 0 a, 1 d, 2 b, 3 b, 4 d, 5 c.
    bool  at_v1     = (corner == 2u) || (corner == 3u) || (corner == 5u);
    bool  left_side = (corner == 0u) || (corner == 2u) || (corner == 3u);
    float width     = at_v1 ? width1 : width0;
    vec2  base_xy   = at_v1 ? v1_in_ndc.xy : v0_in_ndc.xy;
    vec2  side      = left_side ? side_in_ndc : -side_in_ndc;
    vec2  axis      = at_v1 ? axis_in_ndc : -axis_in_ndc;
    vec2  xy        = base_xy + (side + axis) * width;

    gl_Position  = vec4(xy, plane_z(xy, v0_in_ndc, plane_n), 1.0);
    v_line_width = width;

    // Endpoints in viewport-relative pixels for the fragment shader's
    // distance test; top-left framebuffer origins flip y.
    vec2 v0_frag_screen = v0_in_screen;
    vec2 v1_frag_screen = v1_in_screen;
    v0_frag_screen.y = mix(v0_frag_screen.y, vp_size.y - v0_in_screen.y, step(view.vp_y_sign, 0.0));
    v1_frag_screen.y = mix(v1_frag_screen.y, vp_size.y - v1_in_screen.y, step(view.vp_y_sign, 0.0));
    v_start_end = vec4(v0_frag_screen, v1_frag_screen);
}
