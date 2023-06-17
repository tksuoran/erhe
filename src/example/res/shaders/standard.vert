out vec2      v_texcoord;
out vec4      v_position;
out vec4      v_color;
out mat3      v_TBN;
out flat uint v_material_index;
out float     v_tangent_scale;
out float     v_line_width;

out flat uvec2 v_base_color_texture;
out flat uvec2 v_metallic_roughness_texture;


void main()
{
    mat4 world_from_node         ;
    mat4 world_from_node_cofactor;

    if (primitive.primitives[gl_DrawID].skinning_factor < 0.5) {
        world_from_node          = primitive.primitives[gl_DrawID].world_from_node;
        world_from_node_cofactor = primitive.primitives[gl_DrawID].world_from_node_cofactor;
    } else {
        world_from_node =
            a_weights.x * joint.joints[int(a_joints.x)].world_from_bind +
            a_weights.y * joint.joints[int(a_joints.y)].world_from_bind +
            a_weights.z * joint.joints[int(a_joints.z)].world_from_bind +
            a_weights.w * joint.joints[int(a_joints.w)].world_from_bind;
        world_from_node_cofactor =
            a_weights.x * joint.joints[int(a_joints.x)].world_from_bind_cofactor +
            a_weights.y * joint.joints[int(a_joints.y)].world_from_bind_cofactor +
            a_weights.z * joint.joints[int(a_joints.z)].world_from_bind_cofactor +
            a_weights.w * joint.joints[int(a_joints.w)].world_from_bind_cofactor;
    }

    mat4 clip_from_world = camera.cameras[0].clip_from_world;

    vec3 normal          = normalize(vec3(world_from_node_cofactor * vec4(a_normal,      0.0)));
    vec3 tangent         = normalize(vec3(world_from_node_cofactor * vec4(a_tangent.xyz, 0.0)));
    //vec3 bitangent       = normalize(vec3(world_from_node_cofactor * vec4(a_bitangent.xyz, 0.0)));
    vec3 bitangent       = normalize(cross(normal, tangent)) * a_tangent.w;
    vec4 position        = world_from_node * vec4(a_position, 1.0);

    v_tangent_scale  = a_tangent.w;
    v_TBN            = mat3(tangent, bitangent, normal);

    v_position       = position;
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[gl_DrawID].material_index;
    v_texcoord       = a_texcoord;
    v_color          = a_color;
    v_line_width     = primitive.primitives[gl_DrawID].size;
    v_base_color_texture         = material.materials[v_material_index].base_color_texture;
    v_metallic_roughness_texture = material.materials[v_material_index].metallic_roughness_texture;
}
