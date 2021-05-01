out vec4 v_color;
out vec4 v_position;

void main()
{
    gl_Position = model.clip_from_world * vec4(a_position.xyz, 1.0);
    v_position  = a_position;
    v_color     = a_color;
}

