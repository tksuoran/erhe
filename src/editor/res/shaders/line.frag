in vec4 v_color;
in vec4 v_position;

void main(void)
{
    float d              = distance(model.view_position_in_world.xyz, v_position.xyz);
    float distance_scale = min(v_position.w / d, 1.0);
    float alpha          = v_color.a * distance_scale;
    out_color.rgb        = v_color.rgb * alpha;
    out_color.a          = alpha;
}
