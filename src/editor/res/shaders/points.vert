out vec3 v_normal;
out vec4 v_color;

out gl_PerVertex
{
    vec4  gl_Position;
    float gl_PointSize;
};

void main()
{
    mat4 world_from_node        = primitive.primitives[gl_DrawID].world_from_node;
    mat4 world_from_node_normal = primitive.primitives[gl_DrawID].world_from_node_normal;
    mat4 clip_from_world        = camera.cameras[0].clip_from_world;

    vec4 position        = world_from_node * vec4(a_position, 1.0);
    vec3 normal          = normalize(vec3(world_from_node_normal * vec4(a_normal, 0.0)));

    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3  v        = normalize(view_position_in_world - position.xyz);
    float NdotV    = dot(normal, v);
    float d        = distance(view_position_in_world, position.xyz);
    //float max_size = (NdotV > 0.0) ? primitive.primitives[gl_DrawID].size : 0.0; // cull back facing points
    float max_size = primitive.primitives[gl_DrawID].size;
    float bias     = camera.cameras[0].clip_depth_direction * 0.0005 * abs(NdotV);
    v_normal       = normal;
    v_color        = primitive.primitives[gl_DrawID].color;
    gl_Position    = clip_from_world * position;
    gl_Position.z -= bias;
    gl_PointSize   = max(max_size / d, 2.0);
}
