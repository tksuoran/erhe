// erhe_standard_variant.glsl declares ERHE_VARIANT_POSITION_PASS when
// either ERHE_VARIANT_DEPTH_ONLY or ERHE_VARIANT_ID_RENDER is set, so it
// must come before the BxDF / light includes below (the ID-render variant
// skips them entirely).
#include "erhe_standard_variant.glsl"
#if !defined(ERHE_VARIANT_POSITION_PASS)
#include "erhe_bxdf.glsl"
#include "erhe_camera_view.glsl"
#include "erhe_light.glsl"
#include "erhe_math.glsl"   // to_render_target_range()
#include "erhe_srgb.glsl"
#include "erhe_texture.glsl"
#if defined(ERHE_USE_DDGI)
#include "erhe_ddgi.glsl"
#endif
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
// Matching the layout locations standard.vert assigns under the
// ID-render variant.
layout(location = 0) flat in int v_draw_id;
layout(location = 1) flat in int v_primitive_id;
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
vec3 vec3_from_uint(uint i)
{
    uint r = (i >> 16u) & 0xffu;
    uint g = (i >>  8u) & 0xffu;
    uint b = (i >>  0u) & 0xffu;
    return vec3(float(r) / 255.0, float(g) / 255.0, float(b) / 255.0);
}
#endif

#if defined(ERHE_VARIANT_POINTS)
// Flat point color written by standard.vert under the points variant.
layout(location = 0) in vec4 v_point_color;
#endif

#if defined(ERHE_USE_VARYING_POSITION)
layout(location = 0) in vec4      v_position;
#endif

// TODO In the future we might have alpha test which would need texcoord
//      to be passed to fragment shader
#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD0) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 1) in vec2      v_texcoord_0;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec2 v_texcoord_0 = vec2(0.5, 0.5);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD1) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 12) in vec2     v_texcoord_1;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec2 v_texcoord_1 = vec2(0.5, 0.5);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 21) in vec2      v_texcoord_2;            // lightmap UVs
layout(location = 22) flat in vec4 v_lightmap_scale_offset; // atlas region; xy 0 = no lightmap
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec2 v_texcoord_2            = vec2(0.5, 0.5);
const  vec4 v_lightmap_scale_offset = vec4(0.0);
#endif

// Untransformed node-space position for the node_* texgen modes; only
// emitted by standard.vert when a texture slot / feature sources it.
#if defined(ERHE_USE_VERTEX_VARYING_NODE_POSITION) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 24) in vec3 v_node_position;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec3 v_node_position = vec3(0.0);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_COLOR) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 2) in vec4      v_color;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec4 v_color = vec4(1.0);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_ANISO_CONTROL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 3) in vec2      v_aniso_control;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec2 v_aniso_control = vec2(0.0);
#endif

// Neutral const fallbacks (matching the lit-path defaults below) keep the
// ERHE_SELECT_TEXCOORD tangent branch compiling in variants without the
// tangent frame; the branch is dead code there.
#if defined(ERHE_USE_VERTEX_VARYING_TANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 4) in vec3      v_T;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec3 v_T = vec3(1.0, 0.0, 0.0);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 5) in vec3      v_B;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec3 v_B = vec3(0.0, 0.0, 1.0);
#endif

#if defined(ERHE_USE_VERTEX_VARYING_NORMAL) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 6) in vec3      v_N;
#endif

// Scale-only tangent frame consumed by the ERHE_SELECT_TEXCOORD tangent
// branch; standard.vert emits it whenever any texgen axis selects that mode
// (ERHE_USE_TANGENT_TEXGEN, see erhe_standard_variant.glsl). The const
// fallbacks keep the branch compiling in every other variant, where it is
// dead code.
#if defined(ERHE_USE_TANGENT_TEXGEN) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 25) in vec3     v_T_scale_only;
layout(location = 26) in vec3     v_B_scale_only;
// Node-space position with only the node's scale applied; the tangent texgen
// mode projects this rather than the world position.
layout(location = 27) in vec3     v_position_scale_only;
#elif !defined(ERHE_VARIANT_POSITION_PASS)
const  vec3 v_T_scale_only     = vec3(1.0, 0.0, 0.0);
const  vec3 v_B_scale_only     = vec3(0.0, 0.0, 1.0);
const  vec3 v_position_scale_only = vec3(0.0);
#endif

// Selects the texcoord source for a texture / feature from its
// compile-time ERHE_*_TEXGEN_MODE define (an ERHE_TEXGEN_MODE_* value,
// see erhe_standard_variant.glsl); the conditions fold at compile time.
// uv0..uv2 read the vertex texcoord sets, world_* project the world
// position onto an axis plane, node_* the untransformed node-space
// position, and tangent projects the scale-only node position
// (v_position_scale_only) onto the scale-only tangent frame's T/B plane
// (v_T_scale_only / v_B_scale_only). All three come from standard.vert under
// ERHE_USE_TANGENT_TEXGEN: the frame is built from the normal alone rather
// than the authored tangent, and both it and the position see only the scale
// part of world_from_node - so the tangent mode uses neither the world
// v_position nor the shading frame's v_T / v_B, and its texcoords do not
// change as the node is translated or rotated. Every referenced varying has a
// const fallback, so the dead branches always compile.
#if !defined(ERHE_VARIANT_POSITION_PASS)
#define ERHE_SELECT_TEXCOORD(mode) ( \
    ((mode) == ERHE_TEXGEN_MODE_UV1)      ? v_texcoord_1 : \
    ((mode) == ERHE_TEXGEN_MODE_UV2)      ? v_texcoord_2 : \
    ((mode) == ERHE_TEXGEN_MODE_WORLD_XY) ? v_position.xy : \
    ((mode) == ERHE_TEXGEN_MODE_WORLD_XZ) ? v_position.xz : \
    ((mode) == ERHE_TEXGEN_MODE_WORLD_YZ) ? v_position.yz : \
    ((mode) == ERHE_TEXGEN_MODE_NODE_XY)  ? v_node_position.xy : \
    ((mode) == ERHE_TEXGEN_MODE_NODE_XZ)  ? v_node_position.xz : \
    ((mode) == ERHE_TEXGEN_MODE_NODE_YZ)  ? v_node_position.yz : \
    ((mode) == ERHE_TEXGEN_MODE_TANGENT)  ? vec2(dot(v_position_scale_only, v_T_scale_only), dot(v_position_scale_only, v_B_scale_only)) : \
    v_texcoord_0)
#endif

// TODO In the future we might have alpha test which would need material_index
//      to be passed to fragment shader
#if !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 7) flat in uint v_material_index;
#endif

// Debug-visualization varyings. Mirror the gating in standard.vert
// so the link succeeds even when the mesh lacks the underlying
// attributes. Missing attributes fall back to neutral values so the
// debug overrides degrade gracefully.
#if (ERHE_SHADER_DEBUG != 0) && !defined(ERHE_VARIANT_POSITION_PASS)
#  ifdef ERHE_ATTRIBUTE_a_tangent
layout(location =  8) in float      v_tangent_scale;
#  else
const float v_tangent_scale = 1.0;
#  endif
layout(location =  9) in float      v_line_width;
#  ifdef ERHE_USE_SKINNING
layout(location = 10) in vec4       v_bone_color;
#  else
const vec4 v_bone_color = vec4(0.5);
#  endif
#  ifdef ERHE_ATTRIBUTE_a_custom_2
layout(location = 11) flat in uvec2 v_valency_edge_count;
#  else
const uvec2 v_valency_edge_count = uvec2(0u);
#  endif
#endif

// Single-joint weight for the joint_weight_ramp debug mode, written by
// standard.vert (sign-encoded: >= 0 weight, -1 zero-weight alert, -2 no
// target). The const fallback only makes non-skinned variants link;
// their debug branch is empty, so the value is never displayed.
#if (ERHE_SHADER_DEBUG == 34) && !defined(ERHE_VARIANT_POSITION_PASS)
#  ifdef ERHE_USE_SKINNING
layout(location = 23) in float v_weight;
#  else
const float v_weight = -2.0;
#  endif
#endif

// Solid-wireframe varyings (expanded fill mesh): per-vertex barycentric basis,
// per-triangle real-edge mask, and wireframe color / width. See standard.vert.
#if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4) && !defined(ERHE_VARIANT_POSITION_PASS)
layout(location = 13)      in vec3  v_bary;
layout(location = 14) flat in uint  v_edge_mask;
layout(location = 15) flat in vec4  v_wire_color;
layout(location = 16) flat in float v_wire_width;
#endif

// "Lit-path locals are required" gate: true when the BxDF runs lighting
// OR when a debug visualization needs N / V / T / B / sampled
// material properties. The debug overrides read those even in the
// unlit case so they can still show normals, roughness, etc.
#if (ERHE_BXDF_MODEL != ERHE_BXDF_MODEL_UNLIT) || (ERHE_SHADER_DEBUG != 0)
#  define ERHE_USES_LIT_LOCALS 1
#endif

#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2)
// Bicubic lightmap sampling (doc/lightmap_baking_plan.md article
// alignment): cubic B-spline reconstruction via 4 bilinear taps - hides
// the bilinear diamond pattern texel-magnified lightmaps show. The 4x4
// footprint stays inside the chart padding (dilation fills s_padding = 4
// texels; bicubic reads at most 2 outside the chart vs bilinear's 1).
vec3 sample_lightmap_bicubic(vec2 uv)
{
    vec2 texture_size = vec2(textureSize(s_lightmap, 0));
    vec2 texel_size   = vec2(1.0) / texture_size;
    vec2 coord        = uv * texture_size - 0.5;
    vec2 base         = floor(coord);
    vec2 f            = coord - base;
    vec2 f2           = f * f;
    vec2 f3           = f2 * f;
    vec2 w0           = (1.0 / 6.0) * (-f3 + 3.0 * f2 - 3.0 * f + 1.0);
    vec2 w1           = (1.0 / 6.0) * (3.0 * f3 - 6.0 * f2 + 4.0);
    vec2 w2           = (1.0 / 6.0) * (-3.0 * f3 + 3.0 * f2 + 3.0 * f + 1.0);
    vec2 w3           = (1.0 / 6.0) * f3;
    vec2 g0           = w0 + w1;
    vec2 g1           = w2 + w3;
    // Bilinear tap positions chosen so each tap integrates two texels with
    // the B-spline weight ratio baked into the sub-texel offset.
    vec2 h0           = (base + (w1 / g0) - 0.5) * texel_size;
    vec2 h1           = (base + (w3 / g1) + 1.5) * texel_size;
    vec3 t00          = texture(s_lightmap, vec2(h0.x, h0.y)).rgb;
    vec3 t10          = texture(s_lightmap, vec2(h1.x, h0.y)).rgb;
    vec3 t01          = texture(s_lightmap, vec2(h0.x, h1.y)).rgb;
    vec3 t11          = texture(s_lightmap, vec2(h1.x, h1.y)).rgb;
    return g0.y * (g0.x * t00 + g1.x * t10) +
           g1.y * (g0.x * t01 + g1.x * t11);
}
#endif

void main()
{
#if defined(ERHE_VARIANT_POINTS)
    // Points (corner points / polygon centroids) are flat-shaded with the
    // per-draw-primitive color; no lighting. The lit machinery and its
    // includes are skipped via ERHE_VARIANT_POSITION_PASS.
    out_color = v_point_color;
    return;
#endif

#if defined(ERHE_VARIANT_ID_RENDER)
    // Pack draw_id + per-vertex facet id into RGB. The draw_id contribution
    // comes from a per-primitive color offset assigned by
    // erhe::scene_renderer::Primitive_buffer when color_source =
    // id_offset; the facet id is the flat output from standard.vert (the GEO
    // facet index, decoded from a_custom_0). Id_renderer::get() unpacks
    // (r << 16) | (g << 8) | b and walks the range table to recover
    // (mesh, primitive_index, facet_id). No lighting, no varyings, no UBO
    // reads beyond the shared primitive block.
    uint facet_id = uint(v_primitive_id);
    vec3 id_rgb   = vec3_from_uint(facet_id);
    vec3 id       = id_rgb + primitive.primitives[v_draw_id].color.xyz;
    out_color     = vec4(id, 1.0);
    return;
#endif

#if defined(ERHE_VARIANT_SHADOW_DISTANCE)
    // Shadow_technique_mode::distance caster (the "bias-free" path). Store the
    // light-space depth with a slope+resolution bias baked in via fwidth (=
    // |ddx|+|ddy|), so the receiver compares with no bias.
    // light_control_block.shadow_distance_bias_coeff is cdd*(1+pcfRadius),
    // direction-aware (cdd = clip_depth_direction). For the orthographic
    // directional light gl_FragCoord.z is linear, so fwidth(z) is a true slope
    // bias; see doc/shadows.md ("distance / fwidth alternative").
    float ls_depth = gl_FragCoord.z;
    out_color = vec4(ls_depth + light_control_block.shadow_distance_bias_coeff * fwidth(ls_depth));
    return;
#endif

#if defined(ERHE_VARIANT_SHADOW_CUBE)
    // Omnidirectional point-light shadow caster: store the raw radial distance
    // from the light to this fragment into the R32F cube face. The light world
    // position is supplied per pass in the light control block. Unrendered
    // texels keep the large clear value, which the receiver reads as "lit".
    out_color = vec4(length(v_position.xyz - light_control_block.point_light_position.xyz));
    return;
#endif

// The lit-path locals + light loops reference v_material_index,
// v_position, v_texcoord_0/1, the material/light/camera blocks, and the
// BxDF/light/srgb/texture includes -- all gated on POSITION_PASS at the
// top of this file. Gate the body on POSITION_PASS too so the ID-render
// variant does not pull them in.
#if !defined(ERHE_VARIANT_POSITION_PASS)
    Material material   = material.materials[v_material_index];
    vec3     base_color = material.base_color.rgb;

#ifdef ERHE_USE_VERTEX_VARYING_COLOR
    base_color *= v_color.rgb;
#endif

#ifdef ERHE_USE_BASE_COLOR_TEXTURE
    uvec2 base_color_texture = material.base_color_texture;
    vec4  base_color_sample  = sample_texture(
        base_color_texture,
        ERHE_SELECT_TEXCOORD(ERHE_BASE_COLOR_TEXGEN_MODE),
        material.base_color_rotation_scale,
        material.base_color_offset
    );
    base_color *= base_color_sample.rgb;
#endif

#ifdef ERHE_USES_LIT_LOCALS
    vec3 view_position_in_world = vec3(
        camera.cameras[c_view_index].world_from_node[3][0],
        camera.cameras[c_view_index].world_from_node[3][1],
        camera.cameras[c_view_index].world_from_node[3][2]
    );

    vec3 V = normalize(view_position_in_world - v_position.xyz);

#  ifdef ERHE_USE_VERTEX_VARYING_TANGENT
    vec3 T = normalize(v_T);
#  else
    vec3 T = vec3(1.0, 0.0, 0.0);
#  endif
#  ifdef ERHE_USE_VERTEX_VARYING_BITANGENT
    vec3 B = normalize(v_B);
#  else
    vec3 B = vec3(0.0, 0.0, 1.0);
#  endif

#  ifdef ERHE_USE_VERTEX_VARYING_NORMAL
    vec3 N = normalize(v_N);
#  else
    vec3 N = vec3(0.0, 1.0, 0.0);
#  endif

    // anisotropy_strength is 1.0 (fully anisotropic / honor
    // material.roughness.y) by default; the circular-brushed-metal
    // block below modulates it down to 0.0 (isotropic) near the
    // UV origin so the singularity in normalize(brushed_uv) does
    // not flip the BRDF anisotropy axis.
    float anisotropy_strength = 1.0;

#  if defined(ERHE_USE_CIRCULAR_BRUSHED_METAL) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT) && defined(ERHE_USE_VERTEX_VARYING_NORMAL)
    {
        // Circular-brushed-metal modelling. The 8.0 multiplier was
        // chosen empirically to put the perceived "centre of the
        // disc" at length(brushed_uv) ~ 0.125 (1/8); above that
        // the surface looks fully anisotropic, below it the
        // contribution falls off so the singularity at the UV
        // origin does not flip the tangent direction. The
        // exponent (0.25) is a perceptual softening.
        const float k_circular_scale = 8.0;
        // The texcoord set holding the per-facet radial coordinates is
        // selected per material (default set 1, where
        // generate_mesh_facet_texture_coordinates writes them).
        vec2  brushed_uv = ERHE_SELECT_TEXCOORD(ERHE_CIRCULAR_BRUSHED_METAL_TEXGEN_MODE);
        // length(brushed_uv) * k_circular_scale can be zero at
        // the UV origin; the normalize below would then be
        // undefined. Epsilon-guard the denominator and zero out
        // T_circular at the same point.
        float circular_radius              = length(brushed_uv);
        float circular_anisotropy_magnitude = pow(circular_radius * k_circular_scale, 0.25);
        vec2  T_circular                    = (circular_radius > 1e-6)
                                              ? (brushed_uv / circular_radius)
                                              : vec2(0.0, 0.0);
        // X is used to modulate anisotropy level:
        //   0.0 -- Anisotropic
        //   1.0 -- Isotropic when approaching texcoord (0, 0)
        // Y is used for tangent space selection/control:
        //   0.0 -- Use geometry T and B (from vertex attribute
        //   1.0 -- Use T and B derived from texcoord
        // GLSL mix(a, b, w) = a * (1 - w) + y * w   w == 0 -> a, w == 1 -> b
        anisotropy_strength = mix(
            1.0,
            min(1.0, circular_anisotropy_magnitude),
            v_aniso_control.y
        ) * v_aniso_control.x;
        // Mix tangent space geometric .. texcoord generated
        vec3 T0 = T;
        vec3 B0 = B;
        T = (circular_radius > 1e-6) ? mix(T0, T_circular.x * T0 + T_circular.y * B0, v_aniso_control.y) : T0;
        B = (circular_radius > 1e-6) ? mix(B0, T_circular.y * T0 - T_circular.x * B0, v_aniso_control.y) : B0;
    }
#  endif

    // The engine_ready BxDF expects very tight specular for
    // metals; ship a relaxed roughness floor (1e-7) so the
    // anisotropic GGX denominators do not blow up. All other
    // BxDFs use the standard 1e-4 floor.
#  if ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_ANISOTROPIC_ENGINE_READY
    const float c_roughness_floor = 1e-7;
#  else
    const float c_roughness_floor = 1e-4;
#  endif
#  ifdef ERHE_USE_METALLIC_ROUGHNESS_TEXTURE
    vec4 metallic_roughness = sample_texture(
        material.metallic_roughness_texture,
        ERHE_SELECT_TEXCOORD(ERHE_METALLIC_ROUGHNESS_TEXGEN_MODE),
        material.metallic_roughness_rotation_scale,
        material.metallic_roughness_offset
    );
    float metallic    = material.metallic * metallic_roughness.b;
    float roughness_x = max(material.roughness.x * metallic_roughness.g, c_roughness_floor);
    float roughness_y = max(material.roughness.y * metallic_roughness.g, c_roughness_floor);
#  else
    float metallic    = material.metallic;
    // Apply the same floor on the no-texture path so the BRDF
    // math (which divides by roughness terms) does not explode
    // at 0.
    float roughness_x = max(material.roughness.x, c_roughness_floor);
    float roughness_y = max(material.roughness.y, c_roughness_floor);
#  endif
    // B6: collapse roughness_y to roughness_x when anisotropy_strength
    // drops to 0 (the circular-brush singularity falls back to
    // isotropic). When anisotropy_strength == 1 the material's own
    // roughness.y is used unchanged.
    roughness_y = mix(roughness_x, roughness_y, anisotropy_strength);

#  ifdef ERHE_USE_NORMAL_TEXTURE
    {
        // The texels become a tangent space normal as
        // `texel * normal_texture_decode_scale + normal_texture_decode_bias`
        // (the UsdUVTexture inputs:scale and inputs:bias of the normal slot;
        // the defaults (2,2,2,2) and (-1,-1,-1,-1) are the mapping glTF
        // fixes). A two-channel encoding takes the .xy of both, which are
        // the scale and bias of the decoded X and Y whichever channels
        // store them.
        //
        // ERHE_NORMAL_TEXTURE_ENCODING is an ERHE_NORMALMAP_ENCODING_*
        // value (see erhe_standard_variant.glsl). Two-channel encodings
        // (KTX2 normal-mode) store an X+Y map and Z is reconstructed from
        // unit length; the channel layout is part of the encoding value
        // (RG = X in R / Y in G (BC5), GA = X in RGB / Y in A (RGBA8,
        // ASTC L+A blocks)). Left-handed (Direct3D-authored) encodings
        // flip Y.
#    if ERHE_NORMALMAP_IS_TWO_CHANNEL(ERHE_NORMAL_TEXTURE_ENCODING)
#      if ERHE_NORMALMAP_IS_XY_IN_RG(ERHE_NORMAL_TEXTURE_ENCODING)
        vec2 nxy = sample_texture(
            material.normal_texture,
            ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE),
            material.normal_rotation_scale,
            material.normal_offset
        ).rg * material.normal_texture_decode_scale.xy + material.normal_texture_decode_bias.xy;
#      else
        vec2 nxy = sample_texture(
            material.normal_texture,
            ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE),
            material.normal_rotation_scale,
            material.normal_offset
        ).ga * material.normal_texture_decode_scale.xy + material.normal_texture_decode_bias.xy;
#      endif
        vec3 ntex = vec3(nxy, sqrt(max(1.0 - dot(nxy, nxy), 0.0)));
#    else
        vec3 ntex = sample_texture(
            material.normal_texture,
            ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE),
            material.normal_rotation_scale,
            material.normal_offset
        ).xyz * material.normal_texture_decode_scale.xyz + material.normal_texture_decode_bias.xyz;
#    endif
#    if ERHE_NORMALMAP_IS_LEFT_HANDED(ERHE_NORMAL_TEXTURE_ENCODING)
        ntex.y  = -ntex.y;
#    endif
        ntex.xy = ntex.xy * material.normal_texture_scale;
        ntex    = normalize(ntex);
#    if defined(ERHE_USE_VERTEX_VARYING_NORMAL) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
        N       = normalize(mat3(T, B, N) * ntex);
#    else
        N       = ntex;
#    endif
    }
#  endif

    // BREAKING vs older standard.frag revisions: emissive is now
    // applied linearly. Earlier code squared material.emissive
    // (emissive *= emissive) to keep extreme HDR values reachable
    // without changing the small-value behaviour, but the change
    // means saved scenes / authored materials with previous
    // emissive intensities will render brighter. Tools that
    // re-export materials should keep this in mind; the linear
    // semantic is intentional and matches glTF
    // KHR_materials_emissive_strength.
    vec3 emissive = material.emissive.rgb;
#  ifdef ERHE_USE_EMISSION_TEXTURE
    emissive *= sample_texture(
        material.emissive_texture,
        ERHE_SELECT_TEXCOORD(ERHE_EMISSIVE_TEXGEN_MODE),
        material.emissive_rotation_scale,
        material.emissive_offset
    ).rgb;
#  endif

#  ifdef ERHE_USE_OCCLUSION_TEXTURE
    float occlusion = sample_texture(
        material.occlusion_texture,
        ERHE_SELECT_TEXCOORD(ERHE_OCCLUSION_TEXGEN_MODE),
        material.occlusion_rotation_scale,
        material.occlusion_offset
    ).r;
#  else
    const float occlusion = 1.0;
#  endif

    float N_dot_V = clamped_dot(N, V);
#endif // ERHE_USES_LIT_LOCALS

    vec3 color;
#if ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_UNLIT
    color = base_color;
#else
    {
        // Compile-time BxDF dispatch. The Anisotropic branches need T
        // and B; if the mesh / variant did not enable the
        // tangent/bitangent varyings, fall back to the isotropic call
        // (using roughness_x) so the shader still links and renders
        // something reasonable.
#  if (ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_ANISOTROPIC_SLOPE) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
        mat3 TBN_t = transpose(mat3(T, B, N));
#    define BXDF_CALL(L_arg) slope_brdf(base_color, roughness_x, roughness_y, metallic, material.reflectance, TBN_t, L_arg, V, T, B, N)
#  elif ((ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_ANISOTROPIC_BRDF) || (ERHE_BXDF_MODEL == ERHE_BXDF_MODEL_ANISOTROPIC_ENGINE_READY)) && defined(ERHE_USE_VERTEX_VARYING_TANGENT) && defined(ERHE_USE_VERTEX_VARYING_BITANGENT)
#    define BXDF_CALL(L_arg) anisotropic_brdf(base_color, roughness_x, roughness_y, metallic, material.reflectance, L_arg, V, T, B, N)
#  else
#    define BXDF_CALL(L_arg) isotropic_brdf(base_color, roughness_x, metallic, material.reflectance, L_arg, V, N)
#  endif

        uint light_offset = 0;

        //color += (0.5 + 0.5 * N.y) * light_block.ambient_light.rgb * base_color;
        // Baked lightmap (doc/lightmap_baking_plan.md phase 5): a primitive
        // with an atlas region (lightmap_scale_offset.xy > 0) replaces the
        // flat ambient term with its baked lighting, sampled at channel-2
        // UVs mapped into the atlas. The bake holds FULL lighting
        // (direct+indirect), so the analytic light loops below are gated
        // off for lightmapped draws - they would double the direct terms.
        vec3 ambient_term   = light_block.ambient_light.rgb;
        bool lightmap_valid = false;
#if defined(ERHE_USE_VERTEX_VARYING_TEXCOORD2)
        if (v_lightmap_scale_offset.x > 0.0) {
            vec2 lightmap_uv = v_texcoord_2 * v_lightmap_scale_offset.xy + v_lightmap_scale_offset.zw;
            // lightmap_flags bit 0: bicubic B-spline reconstruction (Lightmap
            // window checkbox); off = plain bilinear.
            ambient_term = ((light_block.lightmap_flags & 1u) != 0u)
                ? sample_lightmap_bicubic(lightmap_uv)
                : texture(s_lightmap, lightmap_uv).rgb;
            lightmap_valid = true;
        } else if (v_lightmap_scale_offset.x < 0.0) {
            // Sentinel for a lightmapped world-space tile piece whose tile is
            // not resident: flat white fallback instead of the no-lightmap
            // gate (Lightmap_baker::Atlas_layout::display_uv_scale_offset,
            // partitioned mode). Still lightmap_valid, so analytic lights
            // stay gated off - the piece renders flat until its tile loads.
            ambient_term   = vec3(1.0);
            lightmap_valid = true;
        }
#endif
#if defined(ERHE_USE_DDGI)
        // Dynamic diffuse global illumination (doc/ddgi-plan.md phase 6).
        // Mutually exclusive with the baked lightmap: a lightmapped
        // primitive keeps its bake (which already holds full lighting and
        // gates the analytic loops off), everything else replaces the flat
        // ambient term with the probe volume's indirect irradiance. The
        // analytic light loops still run here - DDGI is indirect only.
        if (!lightmap_valid) {
            ambient_term = ddgi_sample_irradiance(v_position.xyz, N, V);
        }
#endif

        color  = ambient_term * base_color * occlusion;
        color += emissive;

        if (!lightmap_valid) {

        // Directional - shadow-mapped prefix
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb * sample_light_visibility(v_position, light_index, N_dot_L);
                color += intensity * BXDF_CALL(L);
            }
        }
        light_offset += uint(ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED);

        // Directional - non-shadow suffix
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.direction_and_outer_spot_cos.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                vec3 intensity = light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }
        light_offset += uint(ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED);

        // Spot - shadow-mapped prefix
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
                float light_visibility  = sample_light_visibility(v_position, light_index, N_dot_L);
                vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb * light_visibility;
                color += intensity * BXDF_CALL(L);
            }
        }
        light_offset += uint(ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED);

        // Spot - non-shadow suffix
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float spot_attenuation  = get_spot_attenuation(-point_to_light, light.direction_and_outer_spot_cos.xyz, light.direction_and_outer_spot_cos.w, light.position_and_inner_spot_cos.w);
                vec3  intensity         = range_attenuation * spot_attenuation * light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }
        light_offset += uint(ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED);

        // Point - shadow-mapped prefix. Sample the omnidirectional shadow cube
        // by the fragment->light direction; the cube-array layer for this light
        // is shadow_index_packed.y (written by light_buffer.cpp).
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                float light_visibility  = sample_point_light_visibility(v_position.xyz, light.position_and_inner_spot_cos.xyz, float(light.shadow_index_packed.y));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb * light_visibility;
                color += intensity * BXDF_CALL(L);
            }
        }
        light_offset += uint(ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED);

        // Point - non-shadow suffix
        for (uint i = 0u; i < uint(ERHE_LIGHT_COUNT_POINT_NOT_SHADOWMAPPED); ++i) {
            uint  light_index    = light_offset + i;
            Light light          = light_block.lights[light_index];
            vec3  point_to_light = light.position_and_inner_spot_cos.xyz - v_position.xyz;
            vec3  L              = normalize(point_to_light);
            float N_dot_L        = dot(N, L);
            if (N_dot_L > 0.0) {
                float range_attenuation = get_range_attenuation(light.radiance_and_range.w, length(point_to_light));
                vec3  intensity         = range_attenuation * light.radiance_and_range.rgb;
                color += intensity * BXDF_CALL(L);
            }
        }

        } // !lightmap_valid - analytic lights gated off for lightmapped draws
#  undef BXDF_CALL
    }
#endif

    float exposure = camera.cameras[c_view_index].exposure;

    // Sampled fragment alpha for alpha-test / screen-door discards and
    // for the alpha_blend pass. The contributing factors are
    // material.opacity, the vertex color alpha (if present), and the
    // base color texture's alpha (if sampled). v_texcoord_0/1 fall back
    // to (0.5, 0.5) when the corresponding varying is not present, so
    // the sample call is always legal.
    float sampled_alpha = material.opacity;
#ifdef ERHE_USE_VERTEX_VARYING_COLOR
    sampled_alpha *= v_color.a;
#endif
#ifdef ERHE_USE_BASE_COLOR_TEXTURE
    sampled_alpha *= base_color_sample.a;
#endif

#if ERHE_MATERIAL_BLENDING_MODE == ERHE_MATERIAL_BLENDING_MODE_ALPHA_TEST
    if (sampled_alpha < material.alpha_cutoff) {
        discard;
    }
#elif ERHE_MATERIAL_BLENDING_MODE == ERHE_MATERIAL_BLENDING_MODE_SCREEN_DOOR
    {
        const float bayer4[16] = float[16](
             0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
            12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
             3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
            15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
        );
        ivec2 pix       = ivec2(gl_FragCoord.xy) & ivec2(3);
        float threshold = bayer4[pix.y * 4 + pix.x];
        if (sampled_alpha < threshold) {
            discard;
        }
    }
#endif

    // Upper bound for the lit colour this shader emits.
    //
    // On the desktop path post-processing (bloom + tonemap, see compose.frag)
    // maps HDR into displayable range afterwards, so the full representable
    // fp16 range is kept and only the unrepresentable part is cut off.
    //
    // Under multiview there is no post-processing pass - Headset_view renders
    // content and overlay straight into the fp16 OpenXR swapchain the Quest
    // compositor scans out, which is an SDR display. Nothing downstream maps
    // HDR down, so radiance above white buys nothing on that path, and leaving
    // it unbounded is what made the navigation gizmo render black where it
    // overlapped a bright specular highlight: the tool / rendertarget overlay
    // is alpha-blended over this colour, and the result degraded to black as
    // the destination grew, fading back to the correct colour as the highlight
    // fell off. Merely making the value fp16-representable (clamping to
    // m_half_max) did NOT fix it, so the exact threshold is a property of the
    // Adreno blend path rather than of Inf; limiting to white does fix it, and
    // costs nothing visible because the compositor clamps above 1.0 anyway.
    // Not reproducible on desktop even with post-processing disabled.
    // Multiview is XR-only in this codebase (see doc/multiview.md,
    // erhe_camera_view.glsl), so it is exactly the no-post-processing path.
#if defined(ERHE_MULTIVIEW)
    const float c_output_max = 1.0;
#else
    const float c_output_max = m_half_max;
#endif

#if ERHE_MATERIAL_BLENDING_MODE == ERHE_MATERIAL_BLENDING_MODE_ALPHA_BLEND
    // Premultiplied alpha: blend state combines with src_factor=ONE. The
    // coverage is sampled_alpha - material.opacity times the vertex color
    // alpha and the base color texture's alpha - which is where a blended
    // material that carries its opacity in the texture (glTF BLEND with an
    // alpha channel, UsdPreviewSurface with inputs:opacity connected to a
    // texture's outputs:a) has its per-fragment coverage.
    out_color.rgb = to_render_target_range(color * exposure * sampled_alpha, c_output_max);
    out_color.a   = sampled_alpha;
#else
    // opaque / alpha_test / screen_door / multiply / add / subtract:
    // emit straight lit color. The fixed-function Color_blend_state on
    // the multiply / add / subtract pipelines combines this with the
    // framebuffer using src.rgb only; the opaque / discard modes write
    // alpha = 1 with blending disabled.
    out_color.rgb = to_render_target_range(color * exposure, c_output_max);
    out_color.a   = 1.0;
#endif

    // Per-viewport debug visualization overrides. Each branch fully
    // replaces the lit color; ERHE_SHADER_DEBUG is a count axis
    // mirroring erhe::scene_renderer::Shader_debug. The variant cache
    // compiles a distinct shader per non-zero value, so all the
    // branches here are dead code under their respective compile.
#if ERHE_SHADER_DEBUG != 0
    {
        // First light is used as the L direction for V/L/H dot-product
        // visualizations and for the shadow-map texel pattern. Skipped
        // when no light is configured.
#  if ((ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED + \
        ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED + \
        ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED + \
        ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED + \
        ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED + \
        ERHE_LIGHT_COUNT_POINT_NOT_SHADOWMAPPED) > 0)
        Light light_0 = light_block.lights[0];
#    if ((ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED + ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED) > 0)
        vec3  point_to_light_0 = light_0.direction_and_outer_spot_cos.xyz;
#    else
        vec3  point_to_light_0 = light_0.position_and_inner_spot_cos.xyz - v_position.xyz;
#    endif
        vec3  L_dbg = normalize(point_to_light_0);
#  else
        vec3  L_dbg = vec3(0.0, 1.0, 0.0);
#  endif

        mat3 TBN   = mat3(T, B, N);
        mat3 TBN_t_dbg = transpose(TBN);
        vec3 wo    = normalize(TBN_t_dbg * V);
        vec3 wi    = normalize(TBN_t_dbg * L_dbg);
        vec3 wg    = normalize(TBN_t_dbg * N);

        const vec3 palette[24] = vec3[24](
            vec3(0.0, 0.0, 0.0),  vec3(1.0, 0.0, 0.0),  vec3(0.0, 1.0, 0.0),  vec3(0.0, 0.0, 1.0),
            vec3(1.0, 1.0, 0.0),  vec3(0.0, 1.0, 1.0),  vec3(1.0, 0.0, 1.0),  vec3(1.0, 1.0, 1.0),
            vec3(0.5, 0.0, 0.0),  vec3(0.0, 0.5, 0.0),  vec3(0.0, 0.0, 0.5),  vec3(0.5, 0.5, 0.0),
            vec3(0.0, 0.5, 0.5),  vec3(0.5, 0.0, 0.5),  vec3(0.5, 0.5, 0.5),  vec3(1.0, 0.5, 0.0),
            vec3(1.0, 0.0, 0.5),  vec3(0.5, 1.0, 0.0),  vec3(0.0, 1.0, 0.5),  vec3(0.5, 0.0, 1.0),
            vec3(0.0, 0.5, 1.0),  vec3(1.0, 1.0, 0.5),  vec3(0.5, 1.0, 1.0),  vec3(1.0, 0.5, 1.0)
        );

#  if ERHE_SHADER_DEBUG == 1   // vertex_normal
#    ifdef ERHE_USE_VERTEX_VARYING_NORMAL
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * normalize(v_N));
#    else
        out_color.rgb = vec3(0.5);
#    endif
#  elif ERHE_SHADER_DEBUG == 2 // fragment_normal
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * N);
#  elif ERHE_SHADER_DEBUG == 3 // normal_texture
#    ifdef ERHE_USE_NORMAL_TEXTURE
#      if ERHE_NORMALMAP_IS_TWO_CHANNEL(ERHE_NORMAL_TEXTURE_ENCODING)
        // Reconstruct the conventional RGB visualization from the X+Y map.
#        if ERHE_NORMALMAP_IS_XY_IN_RG(ERHE_NORMAL_TEXTURE_ENCODING)
        vec2 dbg_nxy = sample_texture(material.normal_texture, ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE)).rg * 2.0 - vec2(1.0);
#        else
        vec2 dbg_nxy = sample_texture(material.normal_texture, ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE)).ga * 2.0 - vec2(1.0);
#        endif
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * vec3(dbg_nxy, sqrt(max(1.0 - dot(dbg_nxy, dbg_nxy), 0.0))));
#      else
        out_color.rgb = srgb_to_linear(sample_texture(material.normal_texture, ERHE_SELECT_TEXCOORD(ERHE_NORMAL_TEXGEN_MODE)).rgb);
#      endif
#    else
        out_color.rgb = vec3(0.5);
#    endif
#  elif ERHE_SHADER_DEBUG == 4 // tangent
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * T);
#  elif ERHE_SHADER_DEBUG == 5 // vertex_tangent_w
        out_color.rgb = vec3(0.5 + 0.5 * v_tangent_scale);
#  elif ERHE_SHADER_DEBUG == 6 // bitangent
        out_color.rgb = srgb_to_linear(vec3(0.5) + 0.5 * B);
#  elif ERHE_SHADER_DEBUG == 7 // texcoord_0
        out_color.rgb = srgb_to_linear(vec3(fract(v_texcoord_0), 0.0));
#  elif ERHE_SHADER_DEBUG == 8 // texcoord_1
        out_color.rgb = srgb_to_linear(vec3(fract(v_texcoord_1), 0.0));
#  elif ERHE_SHADER_DEBUG == 32 // texcoord_2 (lightmap UVs)
        out_color.rgb = srgb_to_linear(vec3(fract(v_texcoord_2), 0.0));
#  elif ERHE_SHADER_DEBUG == 33 // ddgi_irradiance
        // The DDGI term on its own, so leaks / seams / probe artifacts are
        // visible without the direct light on top. Falls back to the flat
        // ambient (i.e. a constant) when no probe volume is active.
#    if defined(ERHE_USE_DDGI)
        out_color.rgb = ddgi_sample_irradiance(v_position.xyz, N, V);
#    else
        out_color.rgb = light_block.ambient_light.rgb;
#    endif
#  elif ERHE_SHADER_DEBUG == 9 // base_color_texture
#    ifdef ERHE_USE_BASE_COLOR_TEXTURE
        out_color.rgb = srgb_to_linear(base_color);
#    else
        out_color.rgb = vec3(0.0);
#    endif
#  elif ERHE_SHADER_DEBUG == 10 // vertex_color_rgb
        out_color.rgb = srgb_to_linear(v_color.rgb);
#  elif ERHE_SHADER_DEBUG == 11 // vertex_color_alpha
        out_color.rgb = vec3(v_color.a);
#  elif ERHE_SHADER_DEBUG == 12 // aniso_strength
        out_color.rgb = vec3(v_aniso_control.x);
#  elif ERHE_SHADER_DEBUG == 13 // aniso_texcoord
        out_color.rgb = vec3(v_aniso_control.y);
#  elif ERHE_SHADER_DEBUG == 14 // vdotn
        out_color.rgb = vec3(max(dot(V, N), 0.0));
#  elif ERHE_SHADER_DEBUG == 15 // ldotn
        out_color.rgb = vec3(max(dot(L_dbg, N), 0.0));
#  elif ERHE_SHADER_DEBUG == 16 // hdotv
        {
            vec3  H_dbg = normalize(L_dbg + V);
            out_color.rgb = vec3(max(dot(H_dbg, N), 0.0));
        }
#  elif ERHE_SHADER_DEBUG == 17 // joint_indices
        out_color.rgb = vec3(1.0);
#  elif ERHE_SHADER_DEBUG == 18 // joint_weights
        out_color.rgb = srgb_to_linear(v_bone_color.rgb);
#  elif ERHE_SHADER_DEBUG == 19 // omega_o
        out_color.rgb = vec3(0.5) + 0.5 * wo;
        out_color.r = 1.0;
#  elif ERHE_SHADER_DEBUG == 20 // omega_i
        out_color.rgb = vec3(0.5) + 0.5 * wi;
        out_color.g = 1.0;
#  elif ERHE_SHADER_DEBUG == 21 // omega_g
        out_color.rgb = vec3(0.5) + 0.5 * wg;
        out_color.b = 1.0;
#  elif ERHE_SHADER_DEBUG == 22 // vertex_valency
        out_color.rgb = palette[v_valency_edge_count.x % 24u];
#  elif ERHE_SHADER_DEBUG == 23 // polygon_edge_count
        out_color.rgb = palette[v_valency_edge_count.y % 24u];
#  elif ERHE_SHADER_DEBUG == 24 // metallic
        out_color.rgb = vec3(metallic);
#  elif ERHE_SHADER_DEBUG == 25 // roughness
        out_color.rgb = vec3(roughness_x);
#  elif ERHE_SHADER_DEBUG == 26 // occlusion
        out_color.rgb = vec3(occlusion);
#  elif ERHE_SHADER_DEBUG == 27 // emissive
        out_color.rgb = emissive;
#  elif ERHE_SHADER_DEBUG == 28 // shadowmap_texels
        {
            uint  light_index   = 0u;
            vec4  light_pos_homog = light_block.lights[light_index].texture_from_world * v_position;
            vec3  light_pos       = light_pos_homog.xyz / light_pos_homog.w;
            vec2  shadowmap_res   = vec2(textureSize(s_shadow_compare, 0).xy);
            vec2  uv              = fract(shadowmap_res * light_pos.xy);
            vec2  pat0            = step(vec2(0.5), uv);
            vec2  pat1            = step(vec2(0.5), vec2(1.0) - uv);
            float v_pat           = pat0.x * pat0.y + pat1.x * pat1.y;
            bool  inside =
                (light_pos.x >= 0.0) && (light_pos.y >= 0.0) &&
                (light_pos.x <= 1.0) && (light_pos.y <= 1.0);
            if (inside) {
                out_color.r += v_pat;
            } else {
                out_color.b += v_pat;
            }
        }
#  elif ERHE_SHADER_DEBUG == 29 // misc
        {
            float N_dot_T = dot(N, T);
            float N_dot_B = dot(N, B);
            float T_dot_B = dot(T, B);
            out_color.rgb = vec3(N_dot_T, N_dot_B, T_dot_B);
        }
#  elif ERHE_SHADER_DEBUG == 30 // shadow_visibility
        {
            // Shadow / light visibility factor for the first shadow-mapped
            // light: 0.0 = fully shadowed, 1.0 = fully lit. The light buckets
            // are ordered directional-shadowed, directional-unshadowed,
            // spot-shadowed, ... so the first shadow-mapped light is light 0
            // when any directional light casts shadows, otherwise the first
            // spot-shadowed light. Point shadows are not sampled and so are
            // not selected here. sample_light_visibility() returns 1.0 when no
            // shadow map is bound, so this shows white when shadows are off.
#    if ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED > 0
            uint  shadow_light_index    = 0u;
            Light shadow_light          = light_block.lights[shadow_light_index];
            vec3  shadow_point_to_light = shadow_light.direction_and_outer_spot_cos.xyz;
            float shadow_N_dot_L        = dot(N, normalize(shadow_point_to_light));
            out_color.rgb = vec3(sample_light_visibility(v_position, shadow_light_index, shadow_N_dot_L));
#    elif ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED > 0
            uint  shadow_light_index    = uint(ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED + ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED);
            Light shadow_light          = light_block.lights[shadow_light_index];
            vec3  shadow_point_to_light = shadow_light.position_and_inner_spot_cos.xyz - v_position.xyz;
            float shadow_N_dot_L        = dot(N, normalize(shadow_point_to_light));
            out_color.rgb = vec3(sample_light_visibility(v_position, shadow_light_index, shadow_N_dot_L));
#    else
            // No shadow-mapped light in this shader variant.
            out_color.rgb = vec3(0.0);
#    endif
        }
#  elif ERHE_SHADER_DEBUG == 31 // vdotn_dim
        // Dimmed (0.3x) version of vdotn, easier on the eyes when
        // editing vertices / edges / faces in mesh-component mode.
        out_color.rgb = vec3(0.3 * max(dot(V, N), 0.0));
#  elif ERHE_SHADER_DEBUG == 34 // joint_weight_ramp
        // Blender-style single-joint weight ramp: hue sweeps 240deg (blue)
        // -> 0deg (red) with weight, brightness 0.5 -> 1.0 through a gamma
        // 1.5 correction (Blender's default viewport weight ramp,
        // overlay_instance.cc; gamma 1.0 would reproduce the classic 2.79
        // piecewise ramp). v_weight < 0 encodes alerts from standard.vert:
        // -1 = zero weight with the zero-black option on (fade in with
        // alert^2, as Blender does), -2 = no target joint selected (dim
        // magenta "missing data"). |N.V| fake shading keeps the surface
        // shape readable under the flat ramp colors. Non-skinned variants
        // deliberately keep their lit color: empty #else.
#    ifdef ERHE_USE_SKINNING
        {
            float w_dbg   = clamp(v_weight, 0.0, 1.0);
            float hue_dbg = (2.0 / 3.0) * (1.0 - w_dbg);
            float val_dbg = pow(0.5 + 0.5 * w_dbg, 1.5);
            vec3  ramp_rgb = val_dbg * clamp(abs(mod(hue_dbg * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
            ramp_rgb = pow(ramp_rgb, vec3(1.0 / 1.5));
            if (v_weight < -1.5) {
                ramp_rgb = vec3(0.35, 0.05, 0.35);
            } else if (v_weight < 0.0) {
                float alert_dbg = clamp(-v_weight, 0.0, 1.0);
                ramp_rgb = mix(ramp_rgb, vec3(0.0), alert_dbg * alert_dbg);
            }
            float shade_dbg = abs(dot(V, N)) * 0.9 + 0.1;
            out_color.rgb = srgb_to_linear(ramp_rgb * shade_dbg);
        }
#    endif
#  endif
        out_color.a = 1.0;
    }
#endif

#if defined(ERHE_SOLID_WIREFRAME) && defined(ERHE_ATTRIBUTE_a_custom_4)
    {
        // Solid wireframe overlaid on the (lit or debug) fill color. Because
        // this fragment IS the fill fragment, the wireframe shares the fill's
        // exact depth and cannot z-fight. Screen-space anti-aliased distance to
        // each triangle edge comes from the barycentric coords; fan diagonals
        // (cleared mask bits) are pushed to distance 1 so no line is drawn.
        vec3  bary_d     = fwidth(v_bary);
        vec3  edge_a     = smoothstep(vec3(0.0), bary_d * max(v_wire_width, 1e-3), v_bary);
        if ((v_edge_mask & 0x1u) == 0u) { edge_a.x = 1.0; }
        if ((v_edge_mask & 0x2u) == 0u) { edge_a.y = 1.0; }
        if ((v_edge_mask & 0x4u) == 0u) { edge_a.z = 1.0; }
        float wire       = 1.0 - min(min(edge_a.x, edge_a.y), edge_a.z);
        float wire_alpha = wire * v_wire_color.a;
        out_color.rgb    = mix(out_color.rgb, srgb_to_linear(v_wire_color.rgb), wire_alpha);
    }
#endif

#endif
}
