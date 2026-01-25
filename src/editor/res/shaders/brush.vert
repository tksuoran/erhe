 layout(location = 0) out vec2      v_texcoord;
 layout(location = 1) out vec4      v_position;
 layout(location = 2) out vec4      v_color;
 layout(location = 3) out mat3      v_TBN;
 layout(location = 6) out flat uint v_material_index;
 layout(location = 7) out float     v_tangent_scale;

void main()
{
    mat4 world_from_node        = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    mat4 world_from_node_normal = primitive.primitives[ERHE_DRAW_ID].world_from_node_normal;
    mat4 clip_from_world        = camera.cameras[0].clip_from_world;

    //vec3 normal      = normalize(vec3(world_from_node_normal * vec4(a_normal,      0.0)));
    //vec3 tangent     = normalize(vec3(world_from_node        * vec4(a_tangent.xyz, 0.0)));
    //vec3 bitangent   = normalize(cross(normal, tangent)) * a_tangent.w;
    vec4 position    = world_from_node * vec4(a_position, 1.0);

    v_tangent_scale  = 1.0;
    v_position       = position;
    v_TBN            = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0));
    gl_Position      = clip_from_world * position;
    v_material_index = primitive.primitives[ERHE_DRAW_ID].material_index;
    v_texcoord       = vec2(0.0, 0.0);
    v_color          = vec4(1.0, 1.0, 1.0, 1.0);
}
