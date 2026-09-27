#ifndef ERHE_DDGI_RAY_GLSL
#define ERHE_DDGI_RAY_GLSL

// The light transport of one DDGI ray, shared by the probe trace
// (ddgi_trace.comp) and the ground-truth reference irradiance
// (ddgi_reference.comp, doc/editor/ddgi.md "Reference irradiance"), so the
// reference differs from the probe field only by the field's
// discretization, never by what a ray sees. The radiance cascades interval
// trace (rc_trace.comp) shades its hits with the same transport over a
// segment [t_min, t_max] of the ray.
//
// The includer must have included erhe_ray_hit.glsl (and everything it
// requires) first.
//
// ddgi_trace_ray_segment() traces the segment [t_min, t_max] of the ray and
// returns the radiance it carries back to its origin in rgb, and in a the
// signed hit distance:
//  - front face hit: shade_surface() with the ray origin as the observer
//    (direct lights with traced shadow rays, ambient x base color,
//    emission; single bounce, no field feedback), a = +hit distance,
//  - backface hit (the ray started inside geometry): zero radiance,
//    a = -hit distance,
//  - miss (nothing within [t_min, t_max]): sky_radiance, a = t_max.
// hit is false exactly for the miss, so a caller that needs to tell a miss
// from a hit at t_max (the radiance cascades interval trace,
// rc_trace.comp) does not have to compare distances.
vec4 ddgi_trace_ray_segment(vec3 origin, vec3 direction, float t_min, float t_max, vec3 sky_radiance, out bool hit)
{
    Hit_surface surface;
    hit = trace_closest_from(origin, direction, t_min, t_max, surface);
    if (!hit) {
        return vec4(sky_radiance, t_max);
    }
    if (surface.backface) {
        return vec4(0.0, 0.0, 0.0, -surface.hit_t);
    }
    // -direction is the view vector: the BxDF is evaluated for an observer
    // sitting at the ray origin, exactly like a camera ray would.
    return vec4(shade_surface(surface, -direction), surface.hit_t);
}

// The whole ray [0, t_max], the DDGI probe and reference ray. The origin is
// a point in free space (a probe, or a surface point already pushed off its
// surface by the reference's normal bias), not a surface the ray leaves:
// t_min = 0, so a surface right at the origin is hit. A probe sitting on a
// face therefore sees that face (as a front or a backface, by which side of
// it the probe is on) instead of looking through it, which is what the
// relocation pass needs to move it off the face to the correct side.
vec4 ddgi_trace_ray_radiance(vec3 origin, vec3 direction, float t_max, vec3 sky_radiance)
{
    bool hit;
    return ddgi_trace_ray_segment(origin, direction, 0.0, t_max, sky_radiance, hit);
}

#endif // ERHE_DDGI_RAY_GLSL
