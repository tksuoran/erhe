// Vertex stage of the geometry-shader edge-line backend
// (Content_wide_line_geometry_renderer). Reads per-mesh and per-camera
// data from the wide-line renderer's own view UBO (binding 3) instead
// of the standard primitive + camera UBOs -- the renderer is
// self-contained and does not borrow Forward_renderer's descriptor set.
//
// Two compiled variants are bound at draw time: vertex_format_skinned
// (defines ERHE_ATTRIBUTE_a_joint_weights_0 / _indices_0, so the
// skinning branch is taken) and vertex_format_not_skinned (no joint
// attribute defines, so the plain transform branch is taken).

#include "erhe_camera_view.glsl"

#if defined(ERHE_ATTRIBUTE_a_joint_weights_0) && defined(ERHE_ATTRIBUTE_a_joint_indices_0)
#include "erhe_skinning.glsl"
#endif

layout(location = 0) out vec4  vs_color;
layout(location = 1) out float vs_line_width;

void main()
{
    mat4 world_from_node;

#if defined(ERHE_ATTRIBUTE_a_joint_weights_0) && defined(ERHE_ATTRIBUTE_a_joint_indices_0)
    mat4 world_from_node_normal;
    erhe_skin_matrices(
        view.base_joint_index,
        uvec4(a_joint_indices_0),
        a_joint_weights_0,
        world_from_node,
        world_from_node_normal
    );
#else
    world_from_node = view.world_from_node;
#endif

    mat4 clip_from_world = view.cameras[c_view_index].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    vec3 view_position_in_world = view.cameras[c_view_index].view_position_in_world.xyz;
    float fov_left              = view.cameras[c_view_index].fov[0];
    float fov_right             = view.cameras[c_view_index].fov[1];
    float fov_width             = fov_right - fov_left;
    float viewport_width        = view.cameras[c_view_index].viewport[2];
    float d                     = distance(view_position_in_world, position.xyz);
    float size                  = view.line_color.w;

#ifdef ERHE_ATTRIBUTE_a_normal_1
#if defined(ERHE_ATTRIBUTE_a_joint_weights_0) && defined(ERHE_ATTRIBUTE_a_joint_indices_0)
    vec3 normal = normalize(vec3(world_from_node_normal * vec4(a_normal_1, 0.0)));
#else
    vec3 normal = normalize(mat3(world_from_node) * a_normal_1);
#endif
    vec3  v     = normalize(view_position_in_world - position.xyz);
    float NdotV = clamp(dot(normal, v), 0.0, 1.0);
    float bias  = 0.0005 * NdotV * NdotV * view.clip_depth_direction;
#else
    float bias  = 0.0;
#endif

    gl_Position   = clip_from_world * position;
    gl_Position.z -= bias;
    vs_color      = vec4(view.line_color.rgb, 1.0);

    // Negative size = fixed screen-space pixel width (no distance scaling).
    // Positive size = distance-scaled width (matches the compute backend's
    // get_line_width()).
    if (size < 0.0) {
        vs_line_width = -size;
    } else {
        float max_size = 4.0 * size;
        vs_line_width  = (1.0 / 1024.0) * viewport_width * max(max_size / d, 1.0) / fov_width;
    }
}
