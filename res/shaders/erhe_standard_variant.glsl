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

// ERHE_VARIANT_POSITION_PASS is a derived gate. Both ERHE_VARIANT_DEPTH_ONLY
// and ERHE_VARIANT_ID_RENDER skip the lit / debug varyings -- the vertex
// shader only needs gl_Position and (for ID render) two tiny flat outputs;
// the fragment shader either has no body (depth-only) or emits a packed
// ID color directly. Use this gate at every "skip lit machinery" #if so
// both variants stay in lock-step.
#if defined(ERHE_VARIANT_DEPTH_ONLY) || defined(ERHE_VARIANT_ID_RENDER) || defined(ERHE_VARIANT_SHADOW_DISTANCE)
#  define ERHE_VARIANT_POSITION_PASS 1
#endif

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
