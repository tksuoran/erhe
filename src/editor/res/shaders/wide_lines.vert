out layout(location = 0) vec4  vs_color;
out layout(location = 1) float vs_line_width;

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
        world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;
    } else {
        world_from_node =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind;
        world_from_node_normal =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_normal +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_normal;
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position, 1.0);
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal_1, 0.0)));

    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );
    float fov_left        = camera.cameras[0].fov[0];
    float fov_right       = camera.cameras[0].fov[1];
    float fov_up          = camera.cameras[0].fov[2];
    float fov_down        = camera.cameras[0].fov[3];
    float fov_width       = fov_right - fov_left;
    float fov_height      = fov_up    - fov_down;
    float viewport_width  = camera.cameras[0].viewport[2];
    float viewport_height = camera.cameras[0].viewport[2];
    vec3  v               = normalize(view_position_in_world - position.xyz);
    float NdotV           = dot(normal, v);
    float d               = distance(view_position_in_world, position.xyz);
    float bias            = 0.0005 * NdotV * NdotV * camera.cameras[0].clip_depth_direction;
    float max_size        = min(4.0 * primitive.primitives[gl_DrawID].size, 20.0);

    gl_Position   = clip_from_world * position;
    gl_Position.z -= bias;
    vs_color      = primitive.primitives[gl_DrawID].color;
    //vs_color      = vec4(0.5 * normal + vec3(0.5), 1.0);
    //vs_color      = vec4(0.0, 0.0, 0.0, 1.0);
    vs_line_width = (1.0 / 1024.0) * viewport_width * max(max_size / d, 1.0) / fov_width; //primitive.primitives[gl_DrawID].size;
}
