in vec3 v_pos_world;
in vec3 v_color;

void main() {
    vec3  L     = normalize(vec3(3.0, 2.0, 1.0));
    vec3  dx    = dFdx(v_pos_world);
    vec3  dy    = dFdy(v_pos_world);
    vec3  N     = normalize(cross(dx, dy));
    float ln    = max(dot(L, N), 0.0);
    out_color.rgb = vec3((0.2 + ln) * v_color);
    out_color.a   = 1.0;
}
