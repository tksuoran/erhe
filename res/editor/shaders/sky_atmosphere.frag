#include "sky_atmosphere_common.glsl"
#include "erhe_camera_view.glsl"

// Physically-based sky fragment stage (Hillaire EGSR 2020). Ray marches the
// atmosphere from a fixed observer altitude along the world-space view ray,
// adding single scattering (earth-shadowed) plus the pre-integrated
// multi-scattering term from the LUT, then composites a sun disc. Outputs HDR
// radiance; the existing bloom + tonemap post-processing turns the bright sun
// disc into a soft glow.
//
// The sun direction + atmosphere parameters come from the camera UBO
// (Sky_parameters); the two LUT samplers (s_transmittance, s_multiscatter)
// are declared by the bind group layout.

layout(location = 0) in highp vec3 v_ray_direction;

vec3 sample_transmittance(float r, float mu)
{
    return texture(s_transmittance, sky_transmittance_params_to_uv(r, mu)).rgb;
}

vec3 sample_multiscatter(float r, float mu_sun)
{
    return texture(s_multiscatter, sky_multiscatter_params_to_uv(r, mu_sun)).rgb;
}

void main()
{
    vec3  ray_dir         = normalize(v_ray_direction);
    vec4  sun             = camera.cameras[c_view_index].sun_direction;
    vec3  sun_dir         = normalize(sun.xyz);
    float sun_illuminance = sun.w;

    vec4  atmo                 = camera.cameras[c_view_index].atmosphere;
    int   num_steps            = max(2, int(atmo.x));
    float observer_altitude_km = atmo.y;
    float cos_sun_radius       = atmo.z;
    float sun_disc_scale       = atmo.w;
    float exposure             = camera.cameras[c_view_index].exposure;

    vec3 ray_origin = vec3(0.0, R_GROUND + observer_altitude_km, 0.0);

    // March range: stop at the planet surface when the ray points below the
    // horizon, otherwise march to the atmosphere top.
    Sky_ray_hit ground_hit  = sky_ray_sphere(ray_origin, ray_dir, R_GROUND);
    bool        hit_ground  = ground_hit.hit && (ground_hit.t_near > 0.0);
    float       t_top       = sky_distance_to_atmosphere_top(ray_origin, ray_dir);
    float       t_max       = hit_ground ? ground_hit.t_near : t_top;

    float cos_theta      = dot(ray_dir, sun_dir);
    float rayleigh_phase = sky_rayleigh_phase(cos_theta);
    float mie_phase      = sky_mie_phase_hg(cos_theta, MIE_G);

    vec3  inscatter  = vec3(0.0);
    vec3  throughput = vec3(1.0);
    float dt = t_max / float(num_steps);
    for (int s = 0; s < num_steps; ++s) {
        float t = (float(s) + 0.5) * dt;
        vec3  p = ray_origin + ray_dir * t;
        float r = length(p);

        Sky_medium m = sky_sample_medium(p);
        vec3 step_trans = exp(-m.extinction * dt);

        float mu_s      = dot(normalize(p), sun_dir);
        vec3  trans_sun = sample_transmittance(r, mu_s);

        // Earth shadow with a smooth terminator (Hillaire common-mistake #11).
        float shadow       = sky_hits_ground(p, sun_dir) ? 0.0 : 1.0;
        float horizon_fade = clamp(mu_s * 10.0 + 0.5, 0.0, 1.0);
        shadow *= horizon_fade;

        vec3 single = (m.rayleigh_scatter * rayleigh_phase + m.mie_scatter * mie_phase) * trans_sun * shadow;
        vec3 multi  = (m.rayleigh_scatter + m.mie_scatter) * sample_multiscatter(r, mu_s);
        vec3 S      = (single + multi) * sun_illuminance;

        vec3 s_int = (S - S * step_trans) / m.extinction;
        inscatter += throughput * s_int;
        throughput *= step_trans;
    }

    // Lambertian ground bounce for below-horizon view rays.
    if (hit_ground) {
        vec3  ground_pos    = ray_origin + ray_dir * t_max;
        vec3  ground_normal = normalize(ground_pos);
        float sun_cos       = max(0.0, dot(ground_normal, sun_dir));
        vec3  trans_sun_g   = sample_transmittance(R_GROUND, dot(ground_normal, sun_dir));
        inscatter += throughput * GROUND_ALBEDO * (sun_cos / SKY_PI) * trans_sun_g * sun_illuminance;
    }

    // Sun disc, only where the view ray escapes to space.
    if (!hit_ground && (cos_theta > cos_sun_radius)) {
        float edge      = smoothstep(cos_sun_radius, mix(cos_sun_radius, 1.0, 0.5), cos_theta);
        float mu_view   = dot(normalize(ray_origin), ray_dir);
        vec3  sun_trans = sample_transmittance(length(ray_origin), mu_view);
        inscatter += throughput * sun_trans * sun_illuminance * sun_disc_scale * edge;
    }

    out_color.rgb = inscatter * exposure;
    out_color.a   = 1.0;
}
