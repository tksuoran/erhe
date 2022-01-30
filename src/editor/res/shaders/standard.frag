in vec2      v_texcoord;
in vec4      v_position;
in vec4      v_color;
in mat3      v_TBN;
in flat uint v_material_index;
in float     v_tangent_scale;
in float     v_line_width;

float sample_light_visibility(
    vec4  position,
    uint  light_index,
    float NdotL)
{
    sampler2DArray s_shadow = sampler2DArray(light_block.shadow_texture);

    Light light = light_block.lights[light_index];
    vec4  position_in_light_texture_homogeneous = light.texture_from_world * position;

    vec4  position_in_light_texture = position_in_light_texture_homogeneous / position_in_light_texture_homogeneous.w;
    float depth_in_light_texture    = position_in_light_texture.z;
    float sampled_depth = texture(
        s_shadow,
        vec3(
            position_in_light_texture.xy,
            float(light_index)
        )
    ).x;

    float bias = 0.0005 * sqrt(1.0 - NdotL * NdotL) / NdotL; // tan(acos(NdotL))
    bias = clamp(bias, 0.0, 0.01);
    if (camera.cameras[0].clip_depth_direction < 0.0)
    {
        if (depth_in_light_texture + bias >= sampled_depth) // reverse depth
        {
            return 1.0;
        }
    }
    else
    {
        if (depth_in_light_texture - bias <= sampled_depth) // forward depth
        {
            return 1.0;
        }
    }
    return 0.0;
}

float srgb_to_linear(float x)
{
    if (x <= 0.04045)
    {
        return x / 12.92;
    }
    else
    {
        return pow((x + 0.055) / 1.055, 2.4);
    }
}

vec3 srgb_to_linear(vec3 v)
{
    return vec3(
        srgb_to_linear(v.r),
        srgb_to_linear(v.g),
        srgb_to_linear(v.b)
    );
}

vec2 srgb_to_linear(vec2 v)
{
    return vec2(
        srgb_to_linear(v.r),
        srgb_to_linear(v.g)
    );
}

const float M_PI = 3.141592653589793;

struct NormalInfo
{
    vec3 ng;   // Geometric normal
    vec3 n;    // Pertubed normal
    vec3 t;    // Pertubed tangent
    vec3 b;    // Pertubed bitangent
};

float clampedDot(vec3 x, vec3 y)
{
    return clamp(dot(x, y), 0.0, 1.0);
}

float max3(vec3 v)
{
    return max(max(v.x, v.y), v.z);
}

struct MaterialInfo
{
    float perceptualRoughness;  // roughness value, as authored by the model creator (input to shader)
    vec3  f0;                   // full reflectance color (n incidence angle)
    float alphaRoughness;       // roughness mapped to a more linear change in the roughness (proposed by [2])
    vec3  albedoColor;
    vec3  f90;                  // reflectance color at grazing angle
    float metallic;
    vec3  n;
    vec3  baseColor;            // getBaseColor()
};

vec3 F_Schlick(vec3 f0, vec3 f90, float VdotH)
{
    return f0 + (f90 - f0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

float V_GGX(
    float NdotL,
    float NdotV,
    float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float GGXV             = NdotL * sqrt(NdotV * NdotV * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGXL             = NdotV * sqrt(NdotL * NdotL * (1.0 - alphaRoughnessSq) + alphaRoughnessSq);
    float GGX              = GGXV  + GGXL;
    if (GGX > 0.0)
    {
        return 0.5 / GGX;
    }
    return 0.0;
}

float V_GGX_anisotropic(
    float NdotL,
    float NdotV,
    float BdotV,
    float TdotV,
    float TdotL,
    float BdotL,
    float anisotropy,
    float at,
    float ab)
{
    float GGXV = NdotL * length(vec3(at * TdotV, ab * BdotV, NdotV));
    float GGXL = NdotV * length(vec3(at * TdotL, ab * BdotL, NdotL));
    float v    = 0.5 / (GGXV + GGXL);
    return clamp(v, 0.0, 1.0);
}

float D_GGX(float NdotH, float alphaRoughness)
{
    float alphaRoughnessSq = alphaRoughness * alphaRoughness;
    float f                = (NdotH * NdotH) * (alphaRoughnessSq - 1.0) + 1.0;
    return alphaRoughnessSq / (M_PI * f * f);
}

float D_GGX_anisotropic(float NdotH, float TdotH, float BdotH, float anisotropy, float at, float ab)
{
    float  a2 = at * ab;
    vec3   f  = vec3(ab * TdotH, at * BdotH, a2 * NdotH);
    float  w2 = a2 / dot(f, f);
    return a2 * w2 * w2 / M_PI;
}

vec3 BRDF_specularGGX(vec3 f0, vec3 f90, float alphaRoughness, float VdotH, float NdotL, float NdotV, float NdotH)
{
    vec3   F   = F_Schlick(f0, f90, VdotH);
    float  Vis = V_GGX(NdotL, NdotV, alphaRoughness);
    float  D   = D_GGX(NdotH, alphaRoughness);
    return F * Vis * D;
}

vec3 BRDF_specularAnisotropicGGX(vec3 f0, vec3 f90, float alphaRoughness,
                                 float VdotH, float NdotL, float NdotV, float NdotH, float BdotV, float TdotV,
                                 float TdotL, float BdotL, float TdotH, float BdotH, float anisotropy)
{
    vec3  F  = F_Schlick(f0, f90, VdotH);

    float  i_V = V_GGX(NdotL, NdotV, alphaRoughness);
    float  i_D = D_GGX(NdotH, alphaRoughness);
    //return F * Vis * D;

    float at = max(alphaRoughness * (1.0 + anisotropy), 0.00001);
    float ab = max(alphaRoughness * (1.0 - anisotropy), 0.00001);
    float a_V  = V_GGX_anisotropic(NdotL, NdotV, BdotV, TdotV, TdotL, BdotL, anisotropy, at, ab);
    float a_D  = D_GGX_anisotropic(NdotH, TdotH, BdotH, anisotropy, at, ab);

  //vec3  iggx = BRDF_specularGGX(f0, f90, alphaRoughness, VdotH, NdotL, NdotV, NdotH);
  //vec3  iggx = F * i_V * i_D;
  //vec3  aggx = F * a_V * a_D;
    vec3  iggx = F * i_V * i_D; // vec3(0.1) * i_D;
    vec3  aggx = F * a_V * a_D; // vec3(0.1) * a_D;
    return mix(iggx, aggx, 0.0);
}

vec3 BRDF_lambertian(vec3 f0, vec3 f90, vec3 diffuseColor, float VdotH)
{
    return (1.0 - F_Schlick(f0, f90, VdotH)) * (diffuseColor / M_PI);
}

float getRangeAttenuation(float range, float distance)
{
    if (range <= 0.0)
    {
        return 1.0 / pow(distance, 2.0); // negative range means unlimited
    }
    return max(min(1.0 - pow(distance / range, 4.0), 1.0), 0.0) / pow(distance, 2.0);
}

float getSpotAttenuation(vec3 pointToLight, vec3 spotDirection, float outerConeCos, float innerConeCos)
{
    float actualCos = dot(normalize(spotDirection), normalize(-pointToLight));
    if (actualCos > outerConeCos)
    {
        if (actualCos < innerConeCos)
        {
            return smoothstep(outerConeCos, innerConeCos, actualCos);
        }
        return 1.0;
    }
    return 0.0;
}

NormalInfo getNormalInfo(vec3 v)
{
    vec3 t  = normalize(v_TBN[0]);
    vec3 b  = normalize(v_TBN[1]);
    vec3 ng = normalize(v_TBN[2]);
    vec3 n  = ng;

    NormalInfo info;
    info.ng = ng;
    info.t  = t;
    info.b  = b;
    info.n  = n;
    return info;
}

vec3 getBaseColor()
{
    return material.materials[v_material_index].base_color.rgb;
}

MaterialInfo getSpecularGlossinessInfo(MaterialInfo info)
{
    //info.f0                  = material.materials[v_material_index].specular_and_roughness.rgb;
    //info.perceptualRoughness = material.materials[v_material_index].glossiness;
    //info.perceptualRoughness = 1.0 - info.perceptualRoughness; // 1 - glossiness
    info.albedoColor         = info.baseColor.rgb * (1.0 - max(max(info.f0.r, info.f0.g), info.f0.b));
    return info;
}

MaterialInfo getMetallicRoughnessInfo(MaterialInfo info, float f0_ior)
{
    info.metallic            = material.materials[v_material_index].metallic;
    info.perceptualRoughness = material.materials[v_material_index].roughness;
    vec3 f0 = vec3(f0_ior); // Achromatic f0 based on IOR.
    info.albedoColor = mix(info.baseColor.rgb * (vec3(1.0) - f0), vec3(0), info.metallic);
    info.f0          = mix(f0, info.baseColor.rgb, info.metallic);
    return info;
}

void main()
{
    vec3 view_position_in_world = vec3(
        camera.cameras[0].world_from_node[3][0],
        camera.cameras[0].world_from_node[3][1],
        camera.cameras[0].world_from_node[3][2]
    );

    vec3       v          = normalize(view_position_in_world - v_position.xyz);
    NormalInfo normalInfo = getNormalInfo(v);
    vec3       n          = normalInfo.n;
    vec3       t          = normalInfo.t;
    vec3       b          = normalInfo.b;
    float      NdotV      = clampedDot(n, v);
    float      TdotV      = clampedDot(t, v);
    float      BdotV      = clampedDot(b, v);
    float      f0_ior     = 0.04; // The default index of refraction of 1.5 yields a dielectric normal incidence reflectance of 0.04.

    Material material = material.materials[v_material_index];

    MaterialInfo materialInfo;
    materialInfo.baseColor = getBaseColor();
    materialInfo = getSpecularGlossinessInfo(materialInfo);
    materialInfo = getMetallicRoughnessInfo(materialInfo, f0_ior);
    materialInfo.perceptualRoughness = clamp(materialInfo.perceptualRoughness, 0.0, 1.0);
    materialInfo.metallic            = clamp(materialInfo.metallic, 0.0, 1.0);
    materialInfo.alphaRoughness      = materialInfo.perceptualRoughness * materialInfo.perceptualRoughness;

    // Compute reflectance.
    float reflectance = max(max(materialInfo.f0.r, materialInfo.f0.g), materialInfo.f0.b);

    // Anything less than 2% is physically impossible and is instead considered to be shadowing. Compare to "Real-Time-Rendering" 4th editon on page 325.
    materialInfo.f90 = vec3(clamp(reflectance * 50.0, 0.0, 1.0));
    materialInfo.n = n;

    // LIGHTING
    vec3  f_specular = vec3(0.0);
    vec3  f_diffuse  = vec3(0.0);
    vec3  f_emissive = light_block.ambient_light.rgb * materialInfo.baseColor + material.emissive.rgb;
    float anisotropy = material.anisotropy.x;
    float ao         = 1.0;

    uint  directional_light_count  = light_block.directional_light_count;
    uint  spot_light_count         = light_block.spot_light_count;
    uint  directional_light_offset = 0;
    uint  spot_light_offset        = directional_light_count;

    vec3 color = vec3(0);

    for (uint i = 0; i < directional_light_count; ++i)
    {
        uint  light_index  = directional_light_offset + i;
        Light light        = light_block.lights[light_index];
        vec3  pointToLight = light.direction_and_outer_spot_cos.xyz;
        vec3  l            = normalize(pointToLight);   // Direction from surface point to light
        float NdotL        = clampedDot(n, l);
        float NdotV        = clampedDot(n, v);
        if (NdotL > 0.0 || NdotV > 0.0)
        {
            vec3  h         = normalize(l + v);          // Direction of the vector between l and v, called halfway vector
            float NdotH     = clampedDot(n, h);
            float LdotH     = clampedDot(l, h);
            float VdotH     = clampedDot(v, h);
            float TdotL     = dot(t, l);
            float BdotL     = dot(b, l);
            float TdotH     = dot(t, h);
            float BdotH     = dot(b, h);
            vec3  intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, NdotL);
            f_diffuse  += intensity * NdotL * BRDF_lambertian(materialInfo.f0, materialInfo.f90, materialInfo.albedoColor, VdotH);
            f_specular += intensity * NdotL * BRDF_specularAnisotropicGGX(materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness,
                                                                          VdotH, NdotL, NdotV, NdotH,
                                                                          BdotV, TdotV, TdotL, BdotL, TdotH, BdotH, anisotropy);
        }
    }

    for (uint i = 0; i < spot_light_count; ++i)
    {
        uint  light_index  = spot_light_offset + i;
        Light light        = light_block.lights[light_index];
        vec3  pointToLight = light.position_and_inner_spot_cos.xyz - v_position.xyz;
        vec3  l            = normalize(pointToLight);   // Direction from surface point to light
        float NdotL        = clampedDot(n, l);
        float NdotV        = clampedDot(n, v);
        if (NdotL > 0.0 || NdotV > 0.0)
        {
            vec3  h                = normalize(l + v);  // Direction of the vector between l and v, called halfway vector
            float NdotH            = clampedDot(n, h);
            float LdotH            = clampedDot(l, h);
            float VdotH            = clampedDot(v, h);
            float TdotL            = dot(t, l);
            float BdotL            = dot(b, l);
            float TdotH            = dot(t, h);
            float BdotH            = dot(b, h);
            float rangeAttenuation = getRangeAttenuation(light.radiance_and_range.w, length(pointToLight));
            float spotAttenuation  = getSpotAttenuation(-pointToLight, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
            float lightVisibility  = sample_light_visibility(v_position, light_index, NdotL);
            vec3  intensity        = rangeAttenuation * spotAttenuation * light.radiance_and_range.rgb * lightVisibility; // sample_light_visibility(v_position, light_index);

            f_diffuse  += intensity * NdotL * BRDF_lambertian(materialInfo.f0, materialInfo.f90, materialInfo.albedoColor, VdotH);
            f_specular += intensity * NdotL * BRDF_specularAnisotropicGGX(
                materialInfo.f0, materialInfo.f90, materialInfo.alphaRoughness,
                VdotH, NdotL, NdotV, NdotH,
                BdotV, TdotV, TdotL, BdotL, TdotH, BdotH, anisotropy
            );
        }
    }

    color = f_emissive + f_diffuse + f_specular;

    float exposure = camera.cameras[0].exposure;
    out_color.rgb = color * exposure;
    out_color.a = 1.0;
}

//float edge_factor()
//{
//    vec3 d = fwidth(gl_BaryCoordNoPerspNV);
//    vec3 f = step(d * v_line_width, gl_BaryCoordNoPerspNV);
//    return min(min(f.x, f.y), f.z);
//}

//void main_debug()
//{
    //out_color.r = gl_BaryCoordNoPerspNV.r;
    //out_color.g = gl_BaryCoordNoPerspNV.g;
    //out_color.b = gl_BaryCoordNoPerspNV.b;
    //out_color.r = fwidth(gl_BaryCoordNoPerspNV.x) / gl_BaryCoordNoPerspNV.x;
    //out_color.g = fwidth(gl_BaryCoordNoPerspNV.y) / gl_BaryCoordNoPerspNV.y;
    //out_color.b = fwidth(gl_BaryCoordNoPerspNV.z) / gl_BaryCoordNoPerspNV.z;
//    vec3 b = fwidth(gl_BaryCoordNV) / gl_BaryCoordNV;
//    float d = min(b.x, min(b.y, b.z));
//    float e = max(b.x, max(b.y, b.z));
//    out_color.r = e;
//    out_color.g = e;
//    out_color.b = e;
//    out_color.r = fwidth(gl_BaryCoordNoPerspNV.x) / gl_BaryCoordNoPerspNV.x;
//    out_color.g = fwidth(gl_BaryCoordNoPerspNV.y) / gl_BaryCoordNoPerspNV.y;
//    out_color.b = fwidth(gl_BaryCoordNoPerspNV.z) / gl_BaryCoordNoPerspNV.z;
//    out_color.a = 1.0;
//}
