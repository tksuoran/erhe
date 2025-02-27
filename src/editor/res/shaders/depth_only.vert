// Used by Shadow_renderer
void main() {
    mat4 world_from_node;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    } else {
        world_from_node =
            a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x)].world_from_bind +
            a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y)].world_from_bind +
            a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z)].world_from_bind +
            a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w)].world_from_bind;
    }

    mat4 clip_from_world   = light_block.lights[light_control_block.light_index].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position = clip_from_world * position_in_world;
}

