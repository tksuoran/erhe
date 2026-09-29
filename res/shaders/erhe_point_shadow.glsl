#ifndef ERHE_POINT_SHADOW_GLSL
#define ERHE_POINT_SHADOW_GLSL

// Point-light cube shadow geometry shared by the VARIANT_SHADOW_CUBE caster
// (standard.frag) and the receiver (sample_point_light_visibility(),
// erhe_light.glsl). Derivation: doc/erhe/point_light_shadows.md "Stored
// distance" and "Receiver bias".

// Smallest |N . d| (caster plane normal N, texel centre direction d) at
// which the caster stores its plane's distance on the texel centre ray; a
// caster closer to edge-on stores the distance of its interpolated point.
// Receivers inside the R1 grazing limit (|N . L| >= 0.05) meet the centre ray
// at |N . d| >= 0.05 - sqrt(2) / resolution, which the minimum
// point_shadow_resolution (erhe::scene_renderer::c_min_point_shadow_resolution,
// 64) keeps above this by 0.018 rad of normal error.
const float erhe_point_shadow_plane_cos_min = 0.01;

// Index of the largest-magnitude component of direction: the cube face
// axis (ties go to the lower index).
int get_point_shadow_major_axis(vec3 direction)
{
    vec3 abs_direction = abs(direction);
    return (abs_direction.x >= abs_direction.y)
        ? ((abs_direction.x >= abs_direction.z) ? 0 : 2)
        : ((abs_direction.y >= abs_direction.z) ? 1 : 2);
}

// Direction of the centre of the cube texel that contains the direction
// light_to_point, for a cube of `resolution` texels per face edge: the major
// axis of the direction selects the face, the direction scaled to that axis
// gives the face coordinates in [-1, 1], and the centre of their texel is
// returned with the major component +-1 (not normalized). The texel centres
// form the same symmetric grid on every face, whatever the face's (s, t)
// orientation, so no face table is needed; the centre lies half a texel
// from every texel and face boundary, so a cube lookup in this direction
// selects exactly that texel, and a caster fragment's interpolated position
// (at its pixel centre) maps back to its own pixel's centre.
vec3 get_point_shadow_texel_centre(vec3 light_to_point, float resolution)
{
    vec3  abs_direction = abs(light_to_point);
    int   major_axis    = get_point_shadow_major_axis(light_to_point);
    vec3  face_point    = light_to_point / abs_direction[major_axis];
    vec3  face_texel    = clamp(floor(((face_point * 0.5) + 0.5) * resolution), vec3(0.0), vec3(resolution - 1.0));
    vec3  centre        = (((face_texel + 0.5) / resolution) * 2.0) - 1.0;
    centre[major_axis]  = (light_to_point[major_axis] < 0.0) ? -1.0 : 1.0;
    return centre;
}

#endif // ERHE_POINT_SHADOW_GLSL
