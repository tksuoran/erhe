void main()
{
    mat4 world_from_node;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    } else {
        world_from_node =
            a_weights.x * joint.joints[int(a_joints.x)].world_from_bind +
            a_weights.y * joint.joints[int(a_joints.y)].world_from_bind +
            a_weights.z * joint.joints[int(a_joints.z)].world_from_bind +
            a_weights.w * joint.joints[int(a_joints.w)].world_from_bind;
    }

    mat4 clip_from_world   = light_block.lights[light_control_block.light_index].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position = clip_from_world * position_in_world;
}

