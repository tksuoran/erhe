in vec4 v_pos_world;
in flat uint v_instance_id ;
in flat uint v_texel_corner;
in flat uint v_cell_x      ;
in flat uint v_cell_y      ;

in vec2 v_texel_offset;
in vec2 v_position_in_light_texture;

void main() {
    out_color.rgb = vec3(0.4, 0.4, 0.4);
    out_color.a   = 1.0;
}
