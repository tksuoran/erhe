#ifndef ERHE_STANDARD_VARIANT_GLSL
#define ERHE_STANDARD_VARIANT_GLSL

// Documentation header for the editor's standard lit shader variants.
//
// Standard_shader_variants always emits ERHE_STANDARD_VARIANT_BUILD plus
// the per-axis flags / counts that match the (material, mesh, scene) key,
// and Programs::standard itself is now a canonical empty-key variant
// (Standard_variant_key{}) compiled through the same cache. The shader
// source can rely on:
//   - ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X (the underlying
//     attribute is always declared when the varying is enabled),
//   - ERHE_USE_SKINNING => the joint attributes are present,
//   - ERHE_LIGHT_*_*_COUNT being a compile-time integer literal so the
//     light loops unroll or vanish,
//   - ERHE_BXDF_MODEL being a compile-time integer literal mapped from
//     erhe::primitive::Bxdf_model.
//
// No non-variant fallback path exists; if you hit a "undefined macro"
// error in standard.frag / standard.vert, it means a code path is
// compiling them without going through the Standard_shader_variants
// adapter -- fix the call site, do not reintroduce fallback macros.

// Bxdf_model enum values. Keep in sync with erhe::primitive::Bxdf_model.
#define ERHE_BXDF_MODEL_UNLIT             0
#define ERHE_BXDF_MODEL_ISOTROPIC_BRDF    1
#define ERHE_BXDF_MODEL_ANISOTROPIC_BRDF  2

#endif // ERHE_STANDARD_VARIANT_GLSL
