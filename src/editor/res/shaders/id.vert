#define a_id a_custom

flat out vec3 v_id;

void main()
{
    mat4 world_from_node   = primitive.primitives[gl_DrawID].world_from_node;
    mat4 clip_from_world   = camera.cameras[0].clip_from_world;
    vec4 position_in_world = world_from_node * vec4(a_position, 1.0);
    gl_Position            = clip_from_world * position_in_world;
    //// TODO
    ////v_id                   = a_id.rgb + primitive.primitives[gl_DrawID].color.xyz;
}

