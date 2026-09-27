#ifndef ERHE_DDGI_RAY_GLSL
#define ERHE_DDGI_RAY_GLSL

// The light transport of one DDGI ray, shared by the probe trace
// (ddgi_trace.comp) and the ground-truth reference irradiance
// (ddgi_reference.comp, doc/editor/ddgi.md "Reference irradiance"), so the
// reference differs from the probe field only by the field's
// discretization, never by what a ray sees.
//
// The includer must have included erhe_ray_hit.glsl (and everything it
// requires) first.
//
// Returns the radiance the ray carries back to its origin in rgb, and in a
// the signed hit distance:
//  - front face hit: shade_surface() with the ray origin as the observer
//    (direct lights with traced shadow rays, ambient x base color,
//    emission; single bounce, no field feedback), a = +hit distance,
//  - backface hit (the ray started inside geometry): zero radiance,
//    a = -hit distance,
//  - miss (nothing within t_max): sky_radiance, a = t_max.
vec4 ddgi_trace_ray_radiance(vec3 origin, vec3 direction, float t_max, vec3 sky_radiance)
{
    Hit_surface surface;
    // The origin is a point in free space (a probe, or a surface point
    // already pushed off its surface by the reference's normal bias), not a
    // surface the ray leaves: t_min = 0, so a surface right at the origin is
    // hit. A probe sitting on a face therefore sees that face (as a front
    // or a backface, by which side of it the probe is on) instead of looking
    // through it, which is what the relocation pass needs to move it off
    // the face to the correct side.
    if (!trace_closest_from(origin, direction, 0.0, t_max, surface)) {
        return vec4(sky_radiance, t_max);
    }
    if (surface.backface) {
        return vec4(0.0, 0.0, 0.0, -surface.hit_t);
    }
    // -direction is the view vector: the BxDF is evaluated for an observer
    // sitting at the ray origin, exactly like a camera ray would.
    return vec4(shade_surface(surface, -direction), surface.hit_t);
}

#endif // ERHE_DDGI_RAY_GLSL
