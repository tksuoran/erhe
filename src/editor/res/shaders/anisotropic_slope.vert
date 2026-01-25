layout(location = 0) out vec2      v_texcoord;
layout(location = 1) out vec4      v_position;
layout(location = 2) out vec4      v_color;
layout(location = 4) out vec3      v_T;
layout(location = 5) out vec3      v_B;
layout(location = 6) out vec3      v_N;
layout(location = 7) out flat uint v_material_index;

void main()
{
    mat4 world_from_node;
    mat4 world_from_node_normal;

    //if (primitive.primitives[ERHE_DRAW_ID].skinning_factor < 0.5)
    {
        world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
        world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    }
    //else {
    //    world_from_node =
    //        a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind +
    //        a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind;
    //    world_from_node_normal =
    //        a_joint_weights_0.x * joint.joints[int(a_joint_indices_0.x) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
    //        a_joint_weights_0.y * joint.joints[int(a_joint_indices_0.y) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
    //        a_joint_weights_0.z * joint.joints[int(a_joint_indices_0.z) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal +
    //        a_joint_weights_0.w * joint.joints[int(a_joint_indices_0.w) + primitive.primitives[ERHE_DRAW_ID].base_joint_index].world_from_bind_normal;
    //}

    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    //vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal,      0.0)));
    //vec3 tangent         = vec3(world_from_node * vec4(a_tangent.xyz, 0.0));
    //vec3 bitangent       = cross(normal, tangent) * a_tangent.w;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    //v_T              = normalize(tangent  );
    //v_B              = normalize(bitangent);
    //v_N              = normal;
    //v_position       = position;
    //gl_Position      = clip_from_world * position;
    //v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;
    //v_texcoord       = a_texcoord_0;
    //v_color          = a_color_0;

    v_T              = vec3(1.0, 0.0, 0.0);
    v_B              = vec3(0.0, 1.0, 0.0);
    v_N              = vec3(0.0, 0.0, 1.0);
    v_position       = position;
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;
    v_texcoord       = vec2(0.0, 0.0);
    v_color          = vec4(1.0, 1.0, 1.0, 1.0);
}
