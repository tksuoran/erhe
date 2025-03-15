highp in vec4 v_position;

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
        highp vec2 t0 = smoothstep(vec2(0.5), fuzz + vec2(0.5), check_pos);
        highp vec2 t1 = smoothstep(vec2(0.0), fuzz, check_pos);
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
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    highp vec3 pos = v_position.xyz / v_position.w;
    highp vec3 V = normalize(view_position_in_world - pos);
    const float pi = 3.1415926535897932384626433832795;
    highp float elevation = acos(V.y) / pi;      // 0..pi
    highp float heading   = atan(V.x, V.z) / pi; // -pi/2 .. pi/2
    highp float intensity = checkerboard(
        vec2(heading, elevation),
        vec2(18.0, 18.0), 
        0.92,
        1.0
    );

    highp vec3 sky_color;
    //out_color.rgb = 0.5 * V + vec3(0.5);
    if (V.y > 0) {
        highp float ground_factor        = 1.0 - pow(1.0 - max(V.y, 0.0), 8.0);
        highp vec3  ground_nadir_color   = vec3(0.1, 0.1, 0.1);
        highp vec3  ground_horizon_color = vec3(0.2, 0.2, 0.2);
        sky_color = mix(ground_horizon_color, ground_nadir_color, ground_factor);
    } else {
        highp float sky_factor           = 1.0 - pow(1.0 - max(-V.y, 0.0), 10.0);
        highp vec3  sky_horizon_color    = vec3(0.3, 0.3, 0.33);
        highp vec3  sky_zenith_color     = vec3(0.2, 0.2, 0.22);
        sky_color = mix(sky_horizon_color, sky_zenith_color, sky_factor);
    }
    out_color.rgb = intensity * sky_color * camera.cameras[0].exposure;
    out_color.a = 1.0;
}
