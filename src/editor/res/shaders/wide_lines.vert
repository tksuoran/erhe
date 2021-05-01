out vec4  vs_color;
out float vs_line_width;

void main()
{
    mat4 world_from_node = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    vec4 position        = world_from_node * vec4(a_position, 1.0);
    vec3 normal          = normalize(vec3(world_from_node * vec4(a_normal_smooth, 0.0)));

    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );
    vec3  v        = normalize(view_position_in_world - position.xyz);
    float NdotV    = dot(normal, v);
    float d        = distance(view_position_in_world, position.xyz);
    float bias     = 0.0005 * NdotV * NdotV;
    float max_size = 4.0 * primitive.primitives[gl_DrawID].size;

    gl_Position   = clip_from_world * position;
    gl_Position.z += bias; // reverse depth uses += bias, non-reverse should use -= bias
    vs_color      = a_color * primitive.primitives[gl_DrawID].color;
    //vs_color      = vec4(0.5 * normal + vec3(0.5), 1.0);
    //vs_color      = vec4(0.0, 0.0, 0.0, 1.0);
    vs_line_width = max(max_size / d, 1.0); //primitive.primitives[gl_DrawID].size;
}
