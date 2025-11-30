layout(location = 0) in float v_color;

void main() {
    out_color.r = fma(v_color, 0.5, 0.5);
    out_color.gba = vec3(0.0, 0.0, 1.0);
}
