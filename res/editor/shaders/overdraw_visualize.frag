#include "erhe_srgb.glsl"

layout(location = 0) in vec2 v_texcoord;

// The overdraw visualization (doc/plans/overdraw_visualization.md O4): each
// pixel's fragment shader invocation count (overdraw_counts.counts, written
// by the view's counting draws, res/shaders/erhe_overdraw.glsl) through a
// color ramp, and over count 0 the scene color at 25 % luminance, so the
// geometry stays recognizable. The counts and s_input are the view's render
// target size and share its framebuffer row order, so both are read at
// gl_FragCoord. Display-referred: no exposure, bloom or tone mapping.
//
// overdraw_visualize_parameters.parameters.x = max_count: counts from it up
// are white.

vec3 ramp(uint count, uint max_count)
{
    const vec3 steps[6] = vec3[6](
        vec3(0.00, 0.00, 0.00), // 0 black
        vec3(0.05, 0.10, 0.55), // 1 dark blue
        vec3(0.10, 0.70, 0.15), // 2 green
        vec3(0.95, 0.90, 0.10), // 3 yellow
        vec3(1.00, 0.55, 0.05), // 4 orange
        vec3(0.90, 0.08, 0.05)  // 5 red
    );
    if (count >= max_count) {
        return vec3(1.0);
    }
    if (count <= 5u) {
        return steps[count];
    }
    // Red to white between 5 and max_count.
    float t = float(count - 5u) / float(max_count - 5u);
    return mix(steps[5], vec3(1.0), t);
}

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    ivec2 size  = textureSize(s_input, 0);
    uint  count = overdraw_counts.counts[(uint(pixel.y) * uint(size.x)) + uint(pixel.x)];
    uint  max_count = max(overdraw_visualize_parameters.parameters.x, 1u);
    vec3  color;
    if (count == 0u) {
        vec3  scene     = texelFetch(s_input, pixel, 0).rgb;
        float luminance = dot(scene, vec3(0.2126, 0.7152, 0.0722));
        color = vec3(min(0.25 * luminance, 1.0));
    } else {
        color = srgb_to_linear(ramp(count, max_count));
    }
    out_color = vec4(color, 1.0);
}
