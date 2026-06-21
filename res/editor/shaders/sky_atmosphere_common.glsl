#ifndef SKY_ATMOSPHERE_COMMON_GLSL
#define SKY_ATMOSPHERE_COMMON_GLSL

// Shared math for the physically-based procedural sky (Sebastien Hillaire,
// "A Scalable and Production-Ready Sky and Atmosphere Rendering Technique",
// EGSR 2020). Pure functions only: constants, ray-sphere, phase functions,
// medium sampling, and the Bruneton non-linear LUT parameterizations. No
// resource access -- the transmittance / multi-scattering LUTs are sampled
// by the including shader (sampler in the fragment shader, imageLoad in the
// multi-scatter compute shader).
//
// All distances are in kilometres. The planet centre is the origin; world
// +Y is the planet zenith (the editor treats world up = atmosphere up).

const float SKY_PI = 3.1415926535897932384626433832795;

// Atmosphere geometry (Hillaire EGSR 2020, Table 1).
const float R_GROUND = 6360.0; // planet radius (km)
const float R_ATMO   = 6460.0; // atmosphere top radius (km)

// Rayleigh: molecular scattering, wavelength dependent (per km).
const vec3  RAYLEIGH_SCATTER = vec3(5.802e-3, 13.558e-3, 33.1e-3);
const float RAYLEIGH_H       = 8.0; // scale height (km)

// Mie: aerosol scattering, nearly white (per km).
const float MIE_SCATTER = 3.996e-3;
const float MIE_ABSORB  = 0.444e-3;
const float MIE_H       = 1.2; // scale height (km)
const float MIE_G       = 0.8; // forward-peak asymmetry

// Ozone: absorption only, tent profile centred at 25 km, +-15 km wide.
const vec3  OZONE_ABSORB = vec3(0.650e-3, 1.881e-3, 0.085e-3);
const float OZONE_CENTER = 25.0; // peak altitude (km)
const float OZONE_WIDTH  = 15.0; // tent half-width (km)

// Diffuse ground albedo used for the planet-surface bounce (multi-scatter LUT
// and the below-horizon fragment march).
const vec3  GROUND_ALBEDO = vec3(0.3);

// Transmittance LUT dimensions (must match the texture created on the C++
// side and the compute dispatch). Used for the manual bilinear imageLoad in
// the multi-scatter compute shader; the fragment shader uses hardware
// filtering and only needs the UV mapping below.
const float SKY_TRANSMITTANCE_LUT_WIDTH  = 256.0;
const float SKY_TRANSMITTANCE_LUT_HEIGHT = 64.0;
const float SKY_MULTISCATTER_LUT_SIZE    = 32.0;

// ---------------------------------------------------------------------------
// Ray-sphere intersection (sphere centred at the origin). Returns whether the
// ray hits, and the near / far intersection distances along rd (rd assumed
// normalized). t_near may be negative when the origin is inside the sphere.
struct Sky_ray_hit {
    bool  hit;
    float t_near;
    float t_far;
};

Sky_ray_hit sky_ray_sphere(vec3 ro, vec3 rd, float radius)
{
    Sky_ray_hit result;
    result.hit    = false;
    result.t_near = 0.0;
    result.t_far  = 0.0;

    float b = dot(ro, rd);
    float c = dot(ro, ro) - radius * radius;
    float discriminant = b * b - c;
    if (discriminant < 0.0) {
        return result;
    }
    float sqrt_d = sqrt(discriminant);
    result.hit    = true;
    result.t_near = -b - sqrt_d;
    result.t_far  = -b + sqrt_d;
    return result;
}

// Distance from a point inside the atmosphere to the atmosphere top along rd.
float sky_distance_to_atmosphere_top(vec3 ro, vec3 rd)
{
    Sky_ray_hit hit = sky_ray_sphere(ro, rd, R_ATMO);
    return max(0.0, hit.t_far);
}

// True if the ray from ro toward rd is blocked by the planet (t_near > 0).
bool sky_hits_ground(vec3 ro, vec3 rd)
{
    Sky_ray_hit hit = sky_ray_sphere(ro, rd, R_GROUND);
    return hit.hit && (hit.t_near > 0.0);
}

// ---------------------------------------------------------------------------
// Phase functions.
float sky_rayleigh_phase(float cos_theta)
{
    return (3.0 / (16.0 * SKY_PI)) * (1.0 + cos_theta * cos_theta);
}

float sky_mie_phase_hg(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 - g2) / (4.0 * SKY_PI * pow(max(denom, 1e-4), 1.5));
}

// ---------------------------------------------------------------------------
// Medium sampling: scattering and extinction coefficients at a world point.
struct Sky_medium {
    vec3 rayleigh_scatter;
    vec3 mie_scatter;
    vec3 extinction;
};

Sky_medium sky_sample_medium(vec3 pos)
{
    float altitude = length(pos) - R_GROUND;

    float rayleigh_density = exp(-max(0.0, altitude) / RAYLEIGH_H);
    float mie_density      = exp(-max(0.0, altitude) / MIE_H);
    float ozone_density    = max(0.0, 1.0 - abs(altitude - OZONE_CENTER) / OZONE_WIDTH);

    Sky_medium m;
    m.rayleigh_scatter = RAYLEIGH_SCATTER * rayleigh_density;
    m.mie_scatter      = vec3(MIE_SCATTER) * mie_density;

    vec3 mie_absorb   = vec3(MIE_ABSORB) * mie_density;
    vec3 ozone_absorb = OZONE_ABSORB * ozone_density;
    m.extinction = m.rayleigh_scatter + m.mie_scatter + mie_absorb + ozone_absorb;
    // Guard against a zero extinction (analytic step integral divides by it).
    m.extinction = max(m.extinction, vec3(1e-9));
    return m;
}

// ---------------------------------------------------------------------------
// Transmittance LUT parameterization (Bruneton). Forward: (r, mu) -> uv used
// for lookups; inverse: uv -> (r, mu) used when generating the LUT. The two
// are exact inverses; the forward mapping always uses the atmosphere-sphere
// distance (never clipped to the ground -- earth shadow handles below-horizon
// sun directions).
//   r  = view height from the planet centre (km)
//   mu = cosine of the view zenith angle
vec2 sky_transmittance_params_to_uv(float r, float mu)
{
    float H   = sqrt(max(0.0, R_ATMO * R_ATMO - R_GROUND * R_GROUND));
    float rho = sqrt(max(0.0, r * r - R_GROUND * R_GROUND));

    float discriminant = r * r * (mu * mu - 1.0) + R_ATMO * R_ATMO;
    float d     = max(0.0, -r * mu + sqrt(max(0.0, discriminant)));
    float d_min = R_ATMO - r;
    float d_max = rho + H;
    float x_mu  = (d_max > d_min) ? ((d - d_min) / (d_max - d_min)) : 0.0;
    float x_r   = (H > 0.0) ? (rho / H) : 0.0;
    return vec2(x_mu, x_r);
}

void sky_uv_to_transmittance_params(vec2 uv, out float r, out float mu)
{
    float x_mu = uv.x;
    float x_r  = uv.y;

    float H   = sqrt(max(0.0, R_ATMO * R_ATMO - R_GROUND * R_GROUND));
    float rho = H * x_r;
    r = sqrt(max(0.0, rho * rho + R_GROUND * R_GROUND));

    float d_min = R_ATMO - r;
    float d_max = rho + H;
    float d     = d_min + x_mu * (d_max - d_min);
    mu = (d == 0.0) ? 1.0 : ((H * H - rho * rho - d * d) / (2.0 * r * d));
    mu = clamp(mu, -1.0, 1.0);
}

// ---------------------------------------------------------------------------
// Multi-scattering LUT parameterization (linear). x = cos sun-zenith, y =
// normalized altitude.
vec2 sky_multiscatter_params_to_uv(float r, float mu_sun)
{
    float x_mu = mu_sun * 0.5 + 0.5;
    float x_r  = clamp((r - R_GROUND) / (R_ATMO - R_GROUND), 0.0, 1.0);
    return vec2(x_mu, x_r);
}

void sky_uv_to_multiscatter_params(vec2 uv, out float r, out float mu_sun)
{
    mu_sun = clamp(uv.x * 2.0 - 1.0, -1.0, 1.0);
    r      = R_GROUND + clamp(uv.y, 0.0, 1.0) * (R_ATMO - R_GROUND);
}

#endif // SKY_ATMOSPHERE_COMMON_GLSL
