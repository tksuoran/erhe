in vec4 v_position;

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3 pos = v_position.xyz / v_position.w;
    vec3 V = normalize(view_position_in_world - pos);

    vec3 sky_color;
    //out_color.rgb = 0.5 * V + vec3(0.5);
    if (V.y > 0) {
        float ground_factor        = 1.0 - pow(1.0 - max(V.y, 0.0), 8.0);
        vec3  ground_nadir_color   = vec3(0.1, 0.1, 0.1);
        vec3  ground_horizon_color = vec3(0.2, 0.2, 0.2);
        sky_color = mix(ground_horizon_color, ground_nadir_color, ground_factor);
    } else {
        float sky_factor           = 1.0 - pow(1.0 - max(-V.y, 0.0), 10.0);
        vec3  sky_horizon_color    = vec3(0.3, 0.3, 0.33);
        vec3  sky_zenith_color     = vec3(0.2, 0.2, 0.22);
        sky_color = mix(sky_horizon_color, sky_zenith_color, sky_factor);
    }
    out_color.rgb = sky_color * camera.cameras[0].exposure;
    out_color.a = 1.0;
}
