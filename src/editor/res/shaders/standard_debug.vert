out vec2      v_texcoord;
out vec4      v_position;
out vec4      v_color;
out vec2      v_aniso_control;
out mat3      v_TBN;
out flat uint v_material_index;
out float     v_tangent_scale;
out float     v_line_width;

out vec4       v_bone_color;
out flat uvec2 v_valency_edge_count;

void main()
{
    mat4 world_from_node         ;
    mat4 world_from_node_cofactor;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node          = primitive.primitives[gl_DrawID].world_from_node;
        world_from_node_cofactor = primitive.primitives[gl_DrawID].world_from_node_cofactor;
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
    } else {
        world_from_node =
            a_weights.x * joint.joints[int(a_joints.x) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.y * joint.joints[int(a_joints.y) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.z * joint.joints[int(a_joints.z) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind +
            a_weights.w * joint.joints[int(a_joints.w) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind;
        world_from_node_cofactor =
            a_weights.x * joint.joints[int(a_joints.x) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_cofactor +
            a_weights.y * joint.joints[int(a_joints.y) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_cofactor +
            a_weights.z * joint.joints[int(a_joints.z) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_cofactor +
            a_weights.w * joint.joints[int(a_joints.w) + primitive.primitives[gl_DrawID].base_joint_index].world_from_bind_cofactor;
        v_bone_color =
            a_weights.x * joint.debug_joint_colors[(int(a_joints.x) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_weights.y * joint.debug_joint_colors[(int(a_joints.y) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_weights.z * joint.debug_joint_colors[(int(a_joints.z) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_weights.w * joint.debug_joint_colors[(int(a_joints.w) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count];
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;

    vec3 normal          = normalize(vec3(world_from_node_cofactor * vec4(a_normal,        0.0)));
    vec3 tangent         = normalize(vec3(world_from_node_cofactor * vec4(a_tangent.xyz,   0.0)));
    vec3 bitangent       = normalize(cross(normal, tangent)) * a_tangent.w;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    v_tangent_scale  = a_tangent.w;
    v_position       = position;
    v_TBN            = mat3(tangent, bitangent, normal);
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
    v_texcoord       = a_texcoord;
    v_color          = a_color;
    v_aniso_control  = a_aniso_control;
    v_line_width     = primitive.primitives[gl_DrawID].size;
    v_valency_edge_count = a_valency_edge_count;
}
