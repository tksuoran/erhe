layout(location = 0) out vec3  vs_position;
layout(location = 1) out vec4  vs_color;
layout(location = 2) out float vs_line_width;

void main()
{
    mat4 world_from_node;

    //if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5)
    {
        world_from_node = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    }
    //else {
    //    world_from_node =
    //        a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind;
    //}

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position.xyz, 1.0);

    gl_Position   = clip_from_world * position;
    vs_position   = position.xyz;
    vs_line_width = primitive.primitives[ERHE_DRAW_ID].size;
    vs_color      = primitive.primitives[ERHE_DRAW_ID].color;
}
