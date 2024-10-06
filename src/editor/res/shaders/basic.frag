in vec3 v_normal;

void main(void) {
   //vec3  L          = vec3(0.0, 1.0, 0.0);
   //vec3  N          = normalize(v_normal);
   //float ln         = dot(L, N);
   //float ln_clamped = clamp(ln, 0.0, 1.0);
   //vec3  color      = ln_clamped * vec3(1.0, 1.0, 1.0);
   vec3 N = normalize(v_normal);
   vec3 color = max(0.0, N.y) * vec3(1.0, 1.0, 1.0);
   out_color = vec4(color, 1.0);

   //out_color = vec4(srgb_to_linear(0.5 * N + vec3(0.5)), 1.0);
}

