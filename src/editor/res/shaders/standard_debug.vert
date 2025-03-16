#define a_valency_edge_count a_custom_2

layout(location =  0) out vec4       v_position;
layout(location =  1) out vec2       v_texcoord;
layout(location =  2) out vec4       v_color;
layout(location =  3) out vec2       v_aniso_control;
layout(location =  4) out vec3       v_T;
layout(location =  5) out vec3       v_B;
layout(location =  6) out vec3       v_N;
layout(location =  7) out flat uint  v_material_index;
layout(location =  8) out float      v_tangent_scale;
layout(location =  9) out float      v_line_width;
layout(location = 10) out vec4       v_bone_color;
layout(location = 11) out flat uvec2 v_valency_edge_count;

void main() {
    mat4 world_from_node;
    mat4 world_from_node_normal;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
        world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;
        v_bone_color = vec4(0.3, 0.0, 0.3, 1.0);
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
        v_bone_color =
            a_joint_weights_0.x * joint.debug_joint_colors[(int(a_joint_indices_0.x) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.y * joint.debug_joint_colors[(int(a_joint_indices_0.y) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.z * joint.debug_joint_colors[(int(a_joint_indices_0.z) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count] +
            a_joint_weights_0.w * joint.debug_joint_colors[(int(a_joint_indices_0.w) + primitive.primitives[gl_DrawID].base_joint_index) % joint.debug_joint_color_count];
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;

    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal,      0.0)));
    vec3 tangent         = normalize(vec3(world_from_node        * vec4(a_tangent.xyz, 0.0)));
    vec3 bitangent       = normalize(cross(normal, tangent)) * a_tangent.w;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    v_tangent_scale  = a_tangent.w;
    v_position       = position;
    v_T              = tangent;
    v_B              = bitangent;
    v_N              = normal;
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
    v_texcoord       = a_texcoord_0;
    v_color          = a_color_0;
    v_aniso_control  = a_custom_1; //aniso_control;
    v_line_width     = primitive.primitives[gl_DrawID].size;
    v_valency_edge_count = a_valency_edge_count;
}
