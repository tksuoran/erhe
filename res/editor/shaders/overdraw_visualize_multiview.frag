#include "erhe_srgb.glsl"

layout(location = 0) in vec2 v_texcoord;

// The headset's overdraw visualization (doc/plans/overdraw_visualization.md
// "Headset"): drawn over the eye images of the multiview pass, after the
// content passes and before the overlay passes. Each pixel's fragment
// shader invocation count (overdraw_counts.counts, written by the headset's
// counting draws, res/shaders/erhe_overdraw.glsl; view v's rows follow view
// v - 1's) through the color ramp of overdraw_visualize.frag. Over count 0
// the scene stays at 25 % (the blend is premultiplied: color 0, alpha 0.75
// keeps a quarter of the scene color), so the geometry stays recognizable.
//
// overdraw_visualize_parameters.parameters: x = max_count (counts from it up
// are white), y = width, z = height of one view, w != 0: a continuous ramp
// up to max_count (the shader cost mode).

vec3 ramp(uint count, uint max_count, bool continuous)
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
    if (continuous) {
        // Shader cost (doc/plans/shader_cost.md B2): the steps then white, linear up to max_count.
        float position = 6.0 * float(count) / float(max_count);
        uint  step     = min(uint(position), 5u);
        vec3  next     = (step == 5u) ? vec3(1.0) : steps[step + 1u];
        return mix(steps[step], next, position - float(step));
    }
    if (count <= 5u) {
        return steps[count];
    }
    float t = float(count - 5u) / float(max_count - 5u);
    return mix(steps[5], vec3(1.0), t);
}

void main()
{
#if defined(ERHE_MULTIVIEW)
    uint view = uint(gl_ViewIndex);
#else
    uint view = 0u;
#endif
    uvec4 parameters = overdraw_visualize_parameters.parameters;
    uint  max_count  = max(parameters.x, 1u);
    uint  width      = parameters.y;
    uint  height     = parameters.z;
    ivec2 pixel      = ivec2(gl_FragCoord.xy);
    if ((pixel.x < 0) || (pixel.y < 0) || (uint(pixel.x) >= width) || (uint(pixel.y) >= height)) {
        discard;
    }
    uint count = overdraw_counts.counts[(((view * height) + uint(pixel.y)) * width) + uint(pixel.x)];
    if (count == 0u) {
        out_color = vec4(0.0, 0.0, 0.0, 0.75);
    } else {
        out_color = vec4(srgb_to_linear(ramp(count, max_count, overdraw_visualize_parameters.parameters.w != 0u)), 1.0);
    }
}
