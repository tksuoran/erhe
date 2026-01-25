layout(location = 0) out      vec2  v_texcoord;
layout(location = 1) out flat uvec2 v_texture;

void main()
{
    mat4 world_from_node = primitive.primitives[ERHE_DRAW_ID].world_from_node;
    mat4 clip_from_world = camera.cameras[0].clip_from_world;
    uint material_index  = primitive.primitives[ERHE_DRAW_ID].material_index;

    vec4 position = world_from_node * vec4(a_position, 1.0);
    gl_Position   = clip_from_world * position;
    v_texture     = material.materials[material_index].base_color_texture;
    v_texcoord       = vec2(0.0, 0.0);
}
