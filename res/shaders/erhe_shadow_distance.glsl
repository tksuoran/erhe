#ifndef ERHE_SHADOW_DISTANCE_GLSL
#define ERHE_SHADOW_DISTANCE_GLSL

// Distance technique (Shadow_technique_mode::distance) geometry for the 2D
// directional and spot shadow maps, shared by the VARIANT_SHADOW_DISTANCE
// caster (standard.frag) and the receiver (sample_light_visibility(),
// erhe_light.glsl). Derivation: doc/erhe/shadows.md "The distance technique".
//
// Each texel stores the light distance of its caster's primitive plane on the
// texel's centre ray: for a spot light the radial distance from the light
// position, for a directional light the linear distance along the light
// direction from the map's mid-depth plane (texture z = 0.5). Caster and
// receiver build the ray of a texel from the same inputs with the same
// expressions, and the computations are `precise` (no contraction or
// reassociation), so both passes get the same ray bit for bit.

#if __VERSION__ >= 450
#   define ERHE_SHADOW_DISTANCE_PRECISE precise
#else
#   define ERHE_SHADOW_DISTANCE_PRECISE
#endif

// Smallest |N . d| (caster plane normal N, centre ray direction d) at which
// the caster stores its plane's distance on the centre ray; a caster closer
// to edge-on stores the distance of its interpolated point. The receiver
// meets its taps' rays at |N . d| >= 0.05 less the ray spread over the
// filter's reach (doc/erhe/shadows.md "The distance technique", validity).
const float erhe_shadow_distance_plane_cos_min = 0.01;

// Texture coordinates of the centre of the texel with integer-valued index
// `texel` in a map of `resolution` texels per edge.
vec2 get_shadow_distance_texel_centre(vec2 texel, float resolution)
{
    ERHE_SHADOW_DISTANCE_PRECISE vec2 centre = (texel + 0.5) / resolution;
    return centre;
}

// The ray whose light distance a texel stores, in the light's view-relative
// space (positions relative to the light block's view_origin, the light
// camera position; for a spot light that is the light position).
// view_relative_from_texture maps texture (u, v, z, 1) to that space; the ray
// passes through the point of (texel_centre, 0.5). Spot: origin = the light
// position (0), direction = towards that point. Directional: origin = that
// point, direction = the projection's depth axis, oriented away from the
// light (light_direction points towards the light,
// direction_and_outer_spot_cos). The direction is unit length.
void get_shadow_distance_ray(
    mat4     view_relative_from_texture,
    vec3     light_direction,
    bool     is_directional,
    vec2     texel_centre,
    out vec3 ray_origin,
    out vec3 ray_direction
)
{
    ERHE_SHADOW_DISTANCE_PRECISE vec4 homogeneous = view_relative_from_texture * vec4(texel_centre, 0.5, 1.0);
    ERHE_SHADOW_DISTANCE_PRECISE vec3 point       = homogeneous.xyz / homogeneous.w;
    if (is_directional) {
        ERHE_SHADOW_DISTANCE_PRECISE vec3 axis = normalize(view_relative_from_texture[2].xyz);
        ray_origin    = point;
        ray_direction = (dot(axis, light_direction) > 0.0) ? -axis : axis;
    } else {
        ERHE_SHADOW_DISTANCE_PRECISE vec3 direction = normalize(point);
        ray_origin    = vec3(0.0);
        ray_direction = direction;
    }
}

// Light distance of a point that is not taken through a plane: the radial
// distance from the light (spot), or the distance along the ray direction
// from the ray origin's plane (directional).
float get_shadow_distance_of_point(vec3 point, vec3 ray_origin, vec3 ray_direction, bool is_directional)
{
    vec3 origin_to_point = point - ray_origin;
    return is_directional ? dot(origin_to_point, ray_direction) : length(origin_to_point);
}

#endif // ERHE_SHADOW_DISTANCE_GLSL
