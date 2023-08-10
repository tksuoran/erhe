out vec3  vs_position;
out vec4  vs_color;
out float vs_line_width;

void main()
{
    mat4 world_from_node;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    } else {
        world_from_node =
            a_weights.x * joint.joints[int(a_joints.x) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.y * joint.joints[int(a_joints.y) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.z * joint.joints[int(a_joints.z) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.w * joint.joints[int(a_joints.w) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind;
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position.xyz, 1.0);

    gl_Position   = clip_from_world * position;
    vs_position   = position.xyz;
    vs_line_width = primitive.primitives[gl_DrawID].size;
    vs_color      = primitive.primitives[gl_DrawID].color;
}
