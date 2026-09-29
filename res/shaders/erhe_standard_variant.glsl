#ifndef ERHE_STANDARD_VARIANT_GLSL
#define ERHE_STANDARD_VARIANT_GLSL

// Documentation header for the editor's standard lit shader variants.
//
// Shader_variant_cache emits the per-axis flags / counts that match the
// (material, mesh, scene) Shader_key. The shader source can rely on:
//   - ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X (the underlying
//     attribute is always declared when the varying is enabled),
//   - ERHE_USE_SKINNING => the joint attributes are present,
//   - ERHE_LIGHT_COUNT_*_* being a compile-time integer literal so the
//     light loops unroll or vanish,
//   - ERHE_BXDF_MODEL being a compile-time integer literal mapped from
//     erhe::primitive::Bxdf_model.
//
// No non-variant fallback path exists; if you hit an "undefined macro"
// error in standard.frag / standard.vert, it means a code path is
// compiling them without going through Shader_variant_cache -- fix the
// call site, do not reintroduce fallback macros.

// ERHE_VARIANT_POSITION_PASS is a derived gate. ERHE_VARIANT_DEPTH_ONLY,
// ERHE_VARIANT_ID_RENDER and ERHE_VARIANT_POINTS all skip the lit / debug
// varyings -- the vertex shader only needs gl_Position plus, per variant, a
// couple of tiny outputs (ID render: two flat ints; points: gl_PointSize +
// a flat color); the fragment shader either has no body (depth-only) or
// emits a packed ID color / flat point color directly. Use this gate at
// every "skip lit machinery" #if so the variants stay in lock-step.
#if defined(ERHE_VARIANT_DEPTH_ONLY) || defined(ERHE_VARIANT_ID_RENDER) || defined(ERHE_VARIANT_SHADOW_DISTANCE) || defined(ERHE_VARIANT_SHADOW_CUBE) || defined(ERHE_VARIANT_POINTS)
#  define ERHE_VARIANT_POSITION_PASS 1
#endif

// ERHE_VARIANT_SHADOW_CUBE and ERHE_VARIANT_SHADOW_DISTANCE are position
// passes (no lit / debug varyings), but unlike the other position-pass
// variants their fragment shaders need the world position to compute the
// caster plane's distance from the light, so v_position is kept (see
// standard.vert / standard.frag). Use this gate to re-enable the v_position
// varying without pulling in the rest of the lit machinery.
#if !defined(ERHE_VARIANT_POSITION_PASS) || defined(ERHE_VARIANT_SHADOW_CUBE) || defined(ERHE_VARIANT_SHADOW_DISTANCE)
#  define ERHE_USE_VARYING_POSITION 1
#endif

// Texgen_mode enum values. Keep in sync with erhe::primitive::Texgen_mode
// (ERHE_TEXGEN_MODE_* in enums.hpp). Each ERHE_*_TEXGEN_MODE variant axis
// carries one of these; ERHE_SELECT_TEXCOORD in standard.frag maps the
// value to a texcoord source (uv sets, world / node position planes, or a
// tangent-frame projection).
#define ERHE_TEXGEN_MODE_UV0      0
#define ERHE_TEXGEN_MODE_UV1      1
#define ERHE_TEXGEN_MODE_UV2      2
#define ERHE_TEXGEN_MODE_WORLD_XY 3
#define ERHE_TEXGEN_MODE_WORLD_XZ 4
#define ERHE_TEXGEN_MODE_WORLD_YZ 5
#define ERHE_TEXGEN_MODE_NODE_XY  6
#define ERHE_TEXGEN_MODE_NODE_XZ  7
#define ERHE_TEXGEN_MODE_NODE_YZ  8
#define ERHE_TEXGEN_MODE_TANGENT  9

// ERHE_USE_TANGENT_TEXGEN is a derived gate: set when any of the per-slot
// texgen axes selects ERHE_TEXGEN_MODE_TANGENT. That mode must not follow the
// node's rotation or translation, nor the authored tangent direction, so
// standard.vert emits a dedicated set of varyings for it: v_T_scale_only /
// v_B_scale_only, a frame built from the normal transformed by the scale-only
// part of world_from_node, plus v_position_scale_only, the node-space position
// with that same scale applied. ERHE_SELECT_TEXCOORD projects the latter onto
// the former, in place of the world v_position and the shading frame's
// v_T / v_B. Every ERHE_*_TEXGEN_MODE axis is always defined by
// Shader_key::get_defines(), so the test folds at compile time.
#if (ERHE_BASE_COLOR_TEXGEN_MODE             == ERHE_TEXGEN_MODE_TANGENT) || \
    (ERHE_METALLIC_ROUGHNESS_TEXGEN_MODE     == ERHE_TEXGEN_MODE_TANGENT) || \
    (ERHE_NORMAL_TEXGEN_MODE                 == ERHE_TEXGEN_MODE_TANGENT) || \
    (ERHE_OCCLUSION_TEXGEN_MODE              == ERHE_TEXGEN_MODE_TANGENT) || \
    (ERHE_EMISSIVE_TEXGEN_MODE               == ERHE_TEXGEN_MODE_TANGENT) || \
    (ERHE_CIRCULAR_BRUSHED_METAL_TEXGEN_MODE == ERHE_TEXGEN_MODE_TANGENT)
#  define ERHE_USE_TANGENT_TEXGEN 1
#endif

// Normalmap_encoding enum values. Keep in sync with
// erhe::primitive::Normalmap_encoding (ERHE_NORMALMAP_ENCODING_* in
// enums.hpp). RGB encodings store unit normals as RGB = XYZ; X+Y (two
// channel) encodings store X+Y only and the shader reconstructs Z, with
// the channel layout part of the value: GA = X in RGB / Y in A (KTX2
// normal-mode; RGBA8, ASTC L+A blocks), RG = X in R / Y in G (BC5).
// Left-handed (Direct3D-authored) encodings flip Y.
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_THREE_CHANNEL  0
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_GA 1
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_THREE_CHANNEL   2
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA  3
#define ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG 4
#define ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG  5

// Convenience predicates for the encoding axis.
#define ERHE_NORMALMAP_IS_TWO_CHANNEL(e) ( \
    ((e) == ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_GA) || \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA)  || \
    ((e) == ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG) || \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG))
#define ERHE_NORMALMAP_IS_XY_IN_RG(e) ( \
    ((e) == ERHE_NORMALMAP_ENCODING_RIGHT_HANDED_TWO_CHANNEL_RG) || \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG))
#define ERHE_NORMALMAP_IS_LEFT_HANDED(e) ( \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_THREE_CHANNEL) || \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_GA) || \
    ((e) == ERHE_NORMALMAP_ENCODING_LEFT_HANDED_TWO_CHANNEL_RG))

// Bxdf_model enum values. Keep in sync with erhe::primitive::Bxdf_model.
#define ERHE_BXDF_MODEL_UNLIT                    0
#define ERHE_BXDF_MODEL_ISOTROPIC_BRDF           1
#define ERHE_BXDF_MODEL_ANISOTROPIC_BRDF         2
#define ERHE_BXDF_MODEL_ANISOTROPIC_SLOPE        3
#define ERHE_BXDF_MODEL_ANISOTROPIC_ENGINE_READY 4

// Material_blending_mode enum values. Keep in sync with
// erhe::primitive::Material_blending_mode. The fragment shader branches
// on ERHE_MATERIAL_BLENDING_MODE to pick the per-fragment output policy:
//   OPAQUE      -> straight color, alpha = 1.
//   ALPHA_BLEND -> premultiplied color + opacity in alpha (blend state
//                  must enable premultiplied alpha).
//   MULTIPLY    -> lit color (clamped); blend state does dst * src.
//   ADD         -> lit color; blend state adds onto framebuffer.
//   SUBTRACT    -> lit color; blend state does reverse subtract.
//   SCREEN_DOOR -> Bayer 4x4 dithered discard against sampled alpha;
//                  blend stays disabled.
//   ALPHA_TEST  -> hard discard when sampled alpha < material.alpha_cutoff;
//                  blend stays disabled.
#define ERHE_MATERIAL_BLENDING_MODE_OPAQUE       0
#define ERHE_MATERIAL_BLENDING_MODE_ALPHA_BLEND  1
#define ERHE_MATERIAL_BLENDING_MODE_MULTIPLY     2
#define ERHE_MATERIAL_BLENDING_MODE_ADD          3
#define ERHE_MATERIAL_BLENDING_MODE_SUBTRACT     4
#define ERHE_MATERIAL_BLENDING_MODE_SCREEN_DOOR  5
#define ERHE_MATERIAL_BLENDING_MODE_ALPHA_TEST   6

#endif // ERHE_STANDARD_VARIANT_GLSL
