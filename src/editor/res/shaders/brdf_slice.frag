#include "erhe_bxdf.glsl"

in vec2 v_texcoord;

float Sqr(float x) {
    return x * x;
}

vec3 rotate_vector(vec3 v, vec3 axis, float angle) {
    vec3 n;
    axis = normalize(axis);
    n = axis * dot(axis, v);
    return n + cos(angle) * (v - n) + sin(angle) * cross(axis, v);
}

void main() {
    Material material = material.materials[light_block.brdf_material];
    const int   useThetaHSquared = 0;
    const int   useNDotL         = 0;
    const int   showChroma       = 0;
    float phiD             = light_block.brdf_phi_incident_phi.x;
    float incidentPhi      = light_block.brdf_phi_incident_phi.y;
    const float brightness       = 1.0;
    const float exposure         = 0.0;
    const float gamma            = 2.2;

    // orthonormal vectors
    vec3 normal    = vec3(0, 0, 1);
    vec3 tangent   = vec3(1, 0, 0);
    vec3 bitangent = vec3(0, 1, 0);

    // thetaH and thetaD vary from [0 - pi/2]
    const float M_PI = 3.1415926535897932384626433832795;

    float thetaH = v_texcoord.r;

    if (useThetaHSquared != 0) {
        thetaH = Sqr(thetaH) / (M_PI * 0.5);
    }

    float thetaD = v_texcoord.g;

    // compute H from thetaH,phiH where (phiH = incidentPhi)
    float phiH      = incidentPhi;
    float sinThetaH = sin(thetaH);
    float cosThetaH = cos(thetaH);
    float sinPhiH   = sin(phiH);
    float cosPhiH   = cos(phiH);
    vec3 H = vec3(sinThetaH * cosPhiH, sinThetaH * sinPhiH, cosThetaH);

    // compute D from thetaD,phiD
    float sinThetaD = sin(thetaD);
    float cosThetaD = cos(thetaD);
    float sinPhiD   = sin(phiD);
    float cosPhiD   = cos(phiD);
    vec3  D         = vec3(sinThetaD * cosPhiD, sinThetaD * sinPhiD, cosThetaD);

    // compute L by rotating D into incident frame
    vec3 L = rotate_vector(
        rotate_vector(D, bitangent, thetaH),
        normal,
        phiH
    );

    // compute V by reflecting L across H
    vec3 V = 2 * dot(H, L) * H - L;
    vec3 b = anisotropic_brdf(
        material.base_color.rgb,
        material.roughness.x,
        material.roughness.y,
        material.metallic,
        material.reflectance,
        L,
        V,
        tangent,
        bitangent,
        normal
    );

    // apply N . L
    if (useNDotL != 0) {
        b *= clamp(L[2], 0, 1);
    }

    if (showChroma != 0) {
        float norm = max(b[0], max(b[1], b[2]));
        if (norm > 0) {
            b /= norm;
        }
    }

    // brightness
    b *= brightness;

    // exposure
    b *= pow(2.0, exposure);

    // gamma
    b = pow(b, vec3(1.0 / gamma));

    out_color = vec4(
        clamp(b, vec3(0.0), vec3(1.0)),
        1.0
    );
}
