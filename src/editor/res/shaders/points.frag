layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec4 v_color;

void main() {
    //out_color.rgb = 0.5 * v_normal + vec3(0.5);
    //out_color.a = 1.0;
    out_color = v_color;;
}
