#ifndef ERHE_STANDARD_VARIANT_GLSL
#define ERHE_STANDARD_VARIANT_GLSL

// Axis fallbacks for the editor's standard lit shader.
//
// In a variant cache build, Standard_shader_variants has already #defined
// ERHE_STANDARD_VARIANT_BUILD plus the per-axis flags / counts that match
// the (material, mesh, scene) key. In a non-variant build (the editor's
// pre-built `standard` stages compiled by Programs::load_programs), none
// of those defines are present and we fall back to the pre-variant
// behavior:
//   - declare every varying whose source attribute is present in the
//     vertex_format,
//   - enable skinning iff joint_indices_0 + joint_weights_0 are present,
//   - read the per-type light loop bounds out of light_block at runtime.
//
// The variant-cache emitter guarantees ERHE_USE_VERTEX_VARYING_X implies
// ERHE_ATTRIBUTE_a_X (so a varying never references an attribute that
// is not declared) and that ERHE_USE_SKINNING implies the joint
// attributes. The shader source can therefore gate raw attribute reads
// on ERHE_ATTRIBUTE_a_X (still injected automatically from the vertex
// format) and gate varying declarations / the light loops on the
// ERHE_USE_VERTEX_VARYING_X / ERHE_LIGHT_*_*_COUNT macros without any
// knowledge of which build mode is active.

#ifndef ERHE_STANDARD_VARIANT_BUILD

#  if defined(ERHE_ATTRIBUTE_a_normal)
#    define ERHE_USE_VERTEX_VARYING_NORMAL 1
#  endif
#  if defined(ERHE_ATTRIBUTE_a_tangent)
#    define ERHE_USE_VERTEX_VARYING_TANGENT 1
#    if defined(ERHE_ATTRIBUTE_a_normal)
#      define ERHE_USE_VERTEX_VARYING_BITANGENT 1
#    endif
#  endif
#  if defined(ERHE_ATTRIBUTE_a_texcoord_0)
#    define ERHE_USE_VERTEX_VARYING_TEXCOORD0 1
#  endif
#  if defined(ERHE_ATTRIBUTE_a_color_0)
#    define ERHE_USE_VERTEX_VARYING_COLOR 1
#  endif
#  if defined(ERHE_ATTRIBUTE_a_joint_indices_0) && defined(ERHE_ATTRIBUTE_a_joint_weights_0)
#    define ERHE_USE_SKINNING 1
#  endif

#  define ERHE_LIGHT_DIRECTIONAL_COUNT              light_block.directional_light_count
#  define ERHE_LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT light_block.directional_shadow_count
#  define ERHE_LIGHT_SPOT_COUNT                     light_block.spot_light_count
#  define ERHE_LIGHT_SPOT_SHADOWMAPPED_COUNT        light_block.spot_shadow_count
#  define ERHE_LIGHT_POINT_COUNT                    light_block.point_light_count
#  define ERHE_LIGHT_POINT_SHADOWMAPPED_COUNT       light_block.point_shadow_count

#endif // !ERHE_STANDARD_VARIANT_BUILD

#endif // ERHE_STANDARD_VARIANT_GLSL
