#include "erhe_camera_view.glsl"

layout(location = 0) in highp vec4 v_position;

highp float checkerboard(highp vec2 coord, highp vec2 frequency, highp float a, highp float b)
{
    highp vec2  epsilon2 = vec2(0.00004);
    highp float average  = 0.5 * a + 0.5 * b;
    highp vec2  fw       = sqrt(dFdx(coord) * dFdx(coord) + dFdy(coord) * dFdy(coord));
    highp vec2  fuzz     = fw * frequency * 1.0;
    highp float fuzz_max  = max(fuzz.s, fuzz.t);
    highp vec2  check_pos = fract(coord * frequency - epsilon2);

    highp float result;
    if (fuzz_max < 0.5) {
        // At the zenith/nadir heading is pinned to 0 across the quad so
        // dFdx/dFdy of coord.x are zero and fuzz.x is zero. Without the
        // floor, the two smoothsteps below would collapse to
        // smoothstep(e, e, x), which is undefined per the GLSL spec
        // (zero-width edge -> NaN on macOS Metal-backed GL). The NaN
        // then propagates through the bloom pyramid and turns the
        // post-processed sky black.
        highp vec2 fuzz_safe = max(fuzz, vec2(1e-6));
        highp vec2 t0 = smoothstep(vec2(0.5), fuzz_safe + vec2(0.5), check_pos);
        highp vec2 t1 = smoothstep(vec2(0.0), fuzz_safe, check_pos);
        highp vec2 t = t0 + (1.0 - t1);
        result = mix(a, b, t.x * t.y + (1.0 - t.x) * (1.0 - t.y));
        result = mix(result, average, smoothstep(0.125, 0.5, fuzz_max));
    } else {
        result = average;
    }

    return result;
}

void main()
{
    highp vec3 view_position_in_world = vec3(
        camera.cameras[c_view_index].world_from_node[3][0],
        camera.cameras[c_view_index].world_from_node[3][1],
        camera.cameras[c_view_index].world_from_node[3][2]
    );

    highp vec3  pos       = v_position.xyz / v_position.w;
    highp vec3  V0        = normalize(view_position_in_world - pos);
    highp vec3  V         = clamp(V0, vec3(-1.0), vec3(1.0));
    const float pi        = 3.1415926535897932384626433832795;
    highp float epsilon   = 0.000001;
    highp float elevation = acos(V.y) / pi; // 0..pi
    highp float heading;
    if (abs(V.y) > (1.0 - epsilon)) {
        heading = 0.0;
    } else {
        heading = atan(V.x, V.z) / pi; // -pi/2 .. pi/2
    }
    // Sky parameters from the camera UBO:
    //   sky_checker:          x, y = frequency, z = intensity a, w = intensity b
    //   sky_horizon_color:    rgb, w = horizon-to-zenith falloff power
    //   ground_horizon_color: rgb, w = horizon-to-nadir falloff power
    highp vec4 sky_checker          = camera.cameras[c_view_index].sky_checker;
    highp vec4 sky_horizon_color    = camera.cameras[c_view_index].sky_horizon_color;
    highp vec4 sky_zenith_color     = camera.cameras[c_view_index].sky_zenith_color;
    highp vec4 ground_horizon_color = camera.cameras[c_view_index].ground_horizon_color;
    highp vec4 ground_nadir_color   = camera.cameras[c_view_index].ground_nadir_color;

    highp float intensity = checkerboard(
        vec2(heading, elevation),
        sky_checker.xy,
        sky_checker.z,
        sky_checker.w
    );

    highp vec3 sky_color;
    // V.y must be clamped or pow() can result NaN
    if (V.y > 0) {
        highp float ground_factor = 1.0 - pow(1.0 - max(V.y, 0.0), ground_horizon_color.w);
        sky_color = mix(ground_horizon_color.rgb, ground_nadir_color.rgb, ground_factor);
    } else {
        highp float sky_factor    = 1.0 - pow(1.0 - max(-V.y, 0.0), sky_horizon_color.w);
        sky_color = mix(sky_horizon_color.rgb, sky_zenith_color.rgb, sky_factor);
    }
    out_color.rgb = intensity * sky_color * camera.cameras[c_view_index].exposure;
    out_color.a = 1.0;
}
