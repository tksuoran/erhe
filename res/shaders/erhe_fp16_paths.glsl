#ifndef ERHE_FP16_PATHS_GLSL
#define ERHE_FP16_PATHS_GLSL

// The fp16 paths of the standard shader (doc/plans/shader_fp16.md): the
// ERHE_FP16_PATHS variant axis (erhe::scene_renderer::Fp16_path, a bit mask
// of the ERHE_FP16_PATH_* bits below) selects which parts of the shading
// chain compute in float16_t. Each path has a type macro set (mat_*, brdf_*,
// acc_*, att_float, vis_float) that is the half type when the path is live
// and the float type otherwise, so every path's code is written once; a
// value that crosses from one path to another is converted at the boundary
// with the receiving path's type constructor, which is the identity when
// both are fp32. A path is live only on a device with shaderFloat16
// (ERHE_HAS_SHADER_FLOAT16, shader_stages_create_info.cpp), on a lit color
// variant (not a position pass, ERHE_SHADER_DEBUG 0); set_fp16_paths_axis()
// sets the axis to 0 on every other key, and the gate below repeats that so
// a hand-built key compiles too. Shaders that do not define the axis
// (compute shaders) get the fp32 chain.

#include "erhe_half.glsl"

#define ERHE_FP16_PATH_MATERIAL    1   // P1 material inputs after the texture fetches
#define ERHE_FP16_PATH_BRDF        2   // P2 isotropic BRDF
#define ERHE_FP16_PATH_ACCUMULATE  4   // P3 light accumulation, exposure folded in
#define ERHE_FP16_PATH_ATTENUATION 8   // P4 range and spot attenuation polynomials
#define ERHE_FP16_PATH_VISIBILITY  16  // P5 shadow visibility factor after the compare

#if !defined(ERHE_FP16_PATHS)
#  define ERHE_FP16_PATHS 0
#endif
#if !defined(ERHE_SHADER_DEBUG)
#  define ERHE_FP16_PATHS_SHADER_DEBUG 0
#else
#  define ERHE_FP16_PATHS_SHADER_DEBUG ERHE_SHADER_DEBUG
#endif

#if defined(ERHE_HAS_SHADER_FLOAT16) && !defined(ERHE_VARIANT_POSITION_PASS) && (ERHE_FP16_PATHS_SHADER_DEBUG == 0)
#  define ERHE_FP16_PATHS_LIVE ERHE_FP16_PATHS
#else
#  define ERHE_FP16_PATHS_LIVE 0
#endif

#if (ERHE_FP16_PATHS_LIVE & ERHE_FP16_PATH_MATERIAL) != 0
#  define ERHE_FP16_MATERIAL 1
#  define mat_float half
#  define mat_vec3  half_vec3
#else
#  define mat_float float
#  define mat_vec3  vec3
#endif

#if (ERHE_FP16_PATHS_LIVE & ERHE_FP16_PATH_BRDF) != 0
#  define ERHE_FP16_BRDF 1
#  define brdf_float half
#  define brdf_vec3  half_vec3
#else
#  define brdf_float float
#  define brdf_vec3  vec3
#endif

#if (ERHE_FP16_PATHS_LIVE & ERHE_FP16_PATH_ACCUMULATE) != 0
#  define ERHE_FP16_ACCUMULATE 1
#  define acc_float half
#  define acc_vec3  half_vec3
#else
#  define acc_float float
#  define acc_vec3  vec3
#endif

#if (ERHE_FP16_PATHS_LIVE & ERHE_FP16_PATH_ATTENUATION) != 0
#  define ERHE_FP16_ATTENUATION 1
#  define att_float half
#else
#  define att_float float
#endif

#if (ERHE_FP16_PATHS_LIVE & ERHE_FP16_PATH_VISIBILITY) != 0
#  define ERHE_FP16_VISIBILITY 1
#  define vis_float half
#else
#  define vis_float float
#endif

#endif // ERHE_FP16_PATHS_GLSL
