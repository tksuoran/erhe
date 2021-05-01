in vec3 v_normal;

float srgb_to_linear(float x)
{
    if (x <= 0.04045)
    {
        return x / 12.92;
    }
    else
    {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 v)
{
    return vec3(srgb_to_linear(v.x),
                srgb_to_linear(v.y),
                srgb_to_linear(v.z));
}

void main(void)
{
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

