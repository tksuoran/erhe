# Shader variants for editor rendering

Stability: stable

The editor's lit material drawing goes through one uber-shader,
`res/shaders/standard.{vert,frag}`, that is compiled into per-call
variants on demand. The variant key, the cache, and the GLSL contract
that ties them together live in `erhe::scene_renderer`.

## Pieces

- **`Shader_key`** (`shader_key.{hpp,cpp}`): the variant key. Bool axes
  are an enum-indexed `uint32_t` bit mask (`Shader_bool`); integer axes
  are a fixed-size `uint32_t[Shader_int::count]` array. The
  `ERHE_SHADER_BOOL` / `ERHE_SHADER_INT` X-macro lists are the single
  source of truth for which axes exist.
- **`Shader_key::derive(material, vertex_format, mesh_has_skin)`**:
  builds a per-primitive key from the material's bound samplers, BxDF
  model, and flags, plus the vertex format's attribute presence and
  the mesh's skin state. Asymmetric: callers seed the key with the
  scene-level axes (light counts, multiview view count, shader debug
  mode) and `derive` overlays the material + mesh axes on top.
- **`Shader_variant_cache`** (`shader_variant_cache.{hpp,cpp}`):
  `get(key, vertex_format)` returns a cached
  `Reloadable_shader_stages*` or compiles, links, and inserts on
  miss. Compile is synchronous; on link failure `get` returns
  `nullptr` and the renderer falls back to its error shader. Each
  entry is registered with the `Shader_monitor` so edits to
  `standard.{vert,frag}` reload every active variant.
- **`res/shaders/erhe_standard_variant.glsl`**: the GLSL contract that
  the variant cache emits as `#define`s. Declares
  `ERHE_BXDF_MODEL_*` enum values and documents the macro invariants
  the shader source can rely on.

## Variant key dimensions

`Shader_key` combines three sub-keys.

### Material sub-key (boolean axes)

Set by `Shader_key::derive` from the material's bound texture samplers
and flags:

| Macro | Set when |
|-------|----------|
| `ERHE_USE_BASE_COLOR_TEXTURE` | `material.data.texture_samplers.base_color.texture != nullptr` |
| `ERHE_USE_METALLIC_ROUGHNESS_TEXTURE` | same for `metallic_roughness` |
| `ERHE_USE_NORMAL_TEXTURE` | same for `normal` |
| `ERHE_USE_OCCLUSION_TEXTURE` | same for `occlusion` |
| `ERHE_USE_EMISSION_TEXTURE` | same for `emissive` |
| `ERHE_USE_CIRCULAR_BRUSHED_METAL` | `material.use_circular_brushed_metal` |

`ERHE_BXDF_MODEL` (integer axis) is the cast of
`erhe::primitive::Bxdf_model` (`unlit`, `isotropic_brdf`,
`anisotropic_brdf`, `anisotropic_slope`, `anisotropic_engine_ready`).
The GLSL `BXDF_CALL` macro in `standard.frag` dispatches on this
constant.

### Mesh sub-key (boolean axes)

A function of the mesh's `Vertex_format` *and* what the bound material
consumes. The point of the cache: if a material only samples
`base_color` (no normal map, no aniso), the variant does not declare
or pass through tangent / bitangent varyings even when the mesh has
them, and vice versa -- if the material wants a normal map but the
mesh lacks tangents, the normal-mapping path drops out.

| Macro | Set when |
|-------|----------|
| `ERHE_USE_SKINNING` | vertex format has `joint_indices` AND `joint_weights` AND the mesh has a `Skin` attached |
| `ERHE_USE_VERTEX_VARYING_NORMAL` | vertex format has `normal` AND the BxDF is not `unlit` |
| `ERHE_USE_VERTEX_VARYING_TANGENT` | vertex format has `tangent` AND `normal` AND (any texgen source is `tangent` OR (BxDF not `unlit` AND (material has a `normal` sampler OR the BxDF is anisotropic OR `material.use_circular_brushed_metal`))) |
| `ERHE_USE_VERTEX_VARYING_BITANGENT` | same condition as tangent |
| `ERHE_USE_VERTEX_VARYING_TEXCOORD0` | vertex format has `tex_coord,0` AND (any bound material sampler's `texgen_mode` is `uv0` OR the circular-brushed-metal block sources `uv0`); `TEXCOORD1` / `TEXCOORD2` likewise for `uv1` / `uv2` |
| `ERHE_USE_LIGHTMAP` | scene-level environment axis: a lightmap atlas is bound (`Scene_pass_resources::get_lightmap_texture()` non-null). With the vertex format carrying `tex_coord,2` it derives `ERHE_USE_LIGHTMAP_SAMPLING` (`erhe_standard_variant.glsl`), which compiles in `v_texcoord_2`, `v_lightmap_scale_offset` and the atlas sampling; per-primitive `lightmap_scale_offset` then decides which draws sample. Without a bound atlas no variant carries the lightmap path, and binding the first atlas compiles the lightmap variants |
| `ERHE_FLAT_DRAW_COLOR` | environment axis of the color passes, a fragment cost measurement mode (`Color_environment::flat_draw_color`, `Forward_renderer::Draw_parameters::flat_draw_color`; the editor's graphics preset `flat_draw_color`): `standard.vert` passes the draw's primitive record index (`ERHE_PRIMITIVE_INDEX`, location 19) and `standard.frag` writes one of 16 palette colors picked by it and returns before any material, texture, light or shadow map read (an alpha-tested material draws solid). On a color variant it derives `ERHE_VARIANT_FLAT_DRAW_COLOR`, a position pass (`erhe_standard_variant.glsl`): no lit or debug varying is declared or written, the draw index is the only one. Unlike `ERHE_SHADER_DEBUG` it keeps the draw-list path (reads no pass-dependent record field), so GPU culling and occlusion stay on |
| `ERHE_USE_VERTEX_VARYING_NODE_POSITION` | any bound material sampler (or the circular-brushed-metal block) uses a `node_*` texgen mode; forwards the untransformed node-space position for `ERHE_SELECT_TEXCOORD` (`world_*` modes reuse `v_position`) |
| `ERHE_USE_VERTEX_VARYING_COLOR` | vertex format has `color,0` (always pass through; gating left for later) |
| `ERHE_USE_VERTEX_VARYING_ANISO_CONTROL` | vertex format has `custom,custom_attribute_aniso_control` AND `material.use_aniso_control` |

`derive()` guarantees `ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X`
so the attribute-presence gating in shader source remains correct.

#### Debug override: expose all mesh varyings

The material-driven conditions above are bypassed when a debug
visualization is active (`ERHE_SHADER_DEBUG != 0`). The debug modes
visualize raw mesh attributes, so a material that does not consume the
tangent frame (or texcoords, or aniso control) would otherwise force
those varyings off and the debug mode would only ever show its neutral
fallback color. When `SHADER_DEBUG != none`, `derive()` instead turns
on every `ERHE_USE_VERTEX_VARYING_*` axis whose underlying attribute is
present in the `Vertex_format`:

- `NORMAL` when the format has `normal` (ignoring the `unlit` gate),
- `TANGENT` + `BITANGENT` + `NORMAL` when it has `tangent` AND `normal`,
- `TEXCOORD0` / `TEXCOORD1` when it has the corresponding `tex_coord` set,
- `ANISO_CONTROL` when it has the aniso-control custom attribute,
- `COLOR` is already always on when `color,0` is present.

This keeps both invariants intact (`USE_VERTEX_VARYING_X =>
ATTRIBUTE_a_X`, and `BITANGENT => TANGENT && NORMAL`) and has the side
benefit that, under a given debug mode, the varying set depends only on
the vertex format and not on the material. The lit color these varyings
feed is irrelevant under debug because the `#if ERHE_SHADER_DEBUG == N`
block at the end of `standard.frag` fully replaces `out_color`. Debug
varyings gated purely on attribute presence in shader source
(`v_tangent_scale`, `v_valency_edge_count` -- both `#ifdef
ERHE_ATTRIBUTE_a_*`) are unaffected by this and already reach the
fragment shader whenever the mesh carries the attribute.

### Scene sub-key (integer counts)

| Macro | Source |
|-------|--------|
| `ERHE_LIGHT_COUNT_DIRECTIONAL_SHADOWMAPPED` | shadow-mapped prefix size for directional lights |
| `ERHE_LIGHT_COUNT_DIRECTIONAL_NOT_SHADOWMAPPED` | non-shadow tail size for directional lights |
| `ERHE_LIGHT_COUNT_SPOT_SHADOWMAPPED` | shadow-mapped prefix size for spots |
| `ERHE_LIGHT_COUNT_SPOT_NOT_SHADOWMAPPED` | non-shadow tail size for spots |
| `ERHE_LIGHT_COUNT_POINT_SHADOWMAPPED` | shadow-mapped prefix size for points |
| `ERHE_LIGHT_COUNT_POINT_NOT_SHADOWMAPPED` | non-shadow tail size for points |
| `ERHE_SHADER_DEBUG` | `Shader_debug` enum value selecting a debug-visualization override (0 = none) |
| `ERHE_SHADER_MULTIVIEW_COUNT` | view count passed to `Shader_stages_create_info`; 0 / 1 = single-view, >= 2 = multiview |

Counts are **exact** (one variant per observed tuple). No tier
rounding.

### Light shading (`LIGHT_SHADING_CLUSTERED`)

The scene-level boolean axis `ERHE_LIGHT_SHADING_CLUSTERED` selects how
`standard.frag` loops the shaded lights; `set_light_partition_axes()` sets
the light axes of an environment key from the partition's `light_shading`
(`erhe::scene_renderer::Light_shading`, the editor's graphics preset field
`light_shading`).

- Loops (`Light_shading::loops`, the axis clear): six loops whose bounds are
  the six `ERHE_LIGHT_COUNT_*` axes above, over consecutive slots. Every
  partition of the scene's lights is its own variant set, so adding,
  removing or retyping a light compiles new variants.
- Clustered (`Light_shading::clustered`, the axis set): the six
  `ERHE_LIGHT_COUNT_*` axes stay 0. The directional loops take the light
  block's runtime counts (`directional_shadow_count`,
  `directional_light_count`); the spot and point loops walk the fragment's
  light cluster list (`erhe_light_cluster.glsl`, doc/erhe/scene_renderer.md
  `Light_cluster_binner`) in four category runs. One variant set serves
  every light layout, so a light edit compiles nothing. The lists are
  ascending, so a fragment sums the same lights in the same order as the
  loop path, minus lights whose range attenuation is zero there.
- Automatic (`Light_shading::automatic`, the editor preset default): a
  limits value only. `compute_light_layer_partition()` resolves it to
  clustered when more than `automatic_clustered_light_count` (16) lights are
  active, else to loops, so the partition carries one of the two. The
  partition is recomputed when the light set is invalidated (a light added,
  removed or edited) or the limits change, so crossing the threshold switches
  the mode then.

The light-count axes below (the partition, the light array layout
invariant and its literal loop bounds) apply to the loop path only.

### Specular anti-aliasing (`SPECULAR_ANTI_ALIASING`)

The integer axis `ERHE_SPECULAR_ANTI_ALIASING` is the
`erhe::scene_renderer::Specular_anti_aliasing` value (0 off, 1
normal_kernel, 2 finite_difference; doc/erhe/scene_renderer.md "Specular
anti-aliasing"), from the editor's graphics preset field
`specular_anti_aliasing`. Each variant holds only its mode's code, so the
off and normal_kernel variants carry none of finite_difference's per
fragment state (two `mat3` neighbour frames, two view vectors, two position
offsets) and no per-light normalizes.

The environment key carries the preset's mode (`Forward_renderer` and
`Draw_list_renderer` parameters `specular_anti_aliasing`, `Color_environment`,
`Prewarm_parameters`); `set_specular_anti_aliasing_axis()` keeps it only on
keys whose shader calls `anisotropic_brdf()` (`BXDF_MODEL`
`anisotropic_brdf` or `anisotropic_engine_ready` with the tangent and
bitangent varyings) and sets 0 on every other key. `Shader_key::derive()`
applies it, and it is applied again after anything that changes a key's
varying bits or lays the environment over it: `bucket_primitives()` and the
prewarm's extra materials after the force masks, `apply_render_environment()`
in `forward_renderer.cpp` and `Draw_list_scene::resolve_color_stages()`.
The axis therefore always follows the key's final bits.

Variant cost: one extra variant per anisotropic variant per mode in use;
isotropic, slope, unlit, depth, shadow and ID variants never multiply.
Prewarm (`prewarm.cpp`, `Scene_preview::prewarm_variants()`) warms the
preset's mode only. Switching the mode compiles the
anisotropic variants of the new mode once (the default scene: one variant
per switch, `Shader_variant_cache miss` in the log) and switching back
compiles none.

### fp16 paths (`FP16_PATHS`)

The integer axis `ERHE_FP16_PATHS` is a bit mask of
`erhe::scene_renderer::Fp16_path` (1 material inputs, 2 isotropic BRDF, 4
light accumulation, 8 attenuation, 16 shadow visibility factor; the
`ERHE_FP16_PATH_*` constants of `res/shaders/erhe_fp16_paths.glsl`), from
the editor's graphics preset field `shader_fp16_paths` (the Settings window
checkboxes, `set_graphics_preset shader_fp16_paths`). A set bit computes
that part of `standard.frag`'s shading chain in `float16_t`
(doc/plans/shader_fp16.md states each path's boundary and gates). The
header defines one type macro set per path (`mat_float` / `mat_vec3`,
`brdf_float` / `brdf_vec3`, `acc_float` / `acc_vec3`, `att_float`,
`vis_float`): the half type when the path is live, the float type
otherwise, so each path's code is written once and a value crossing into
another path is converted with the receiving path's constructor (the
identity in fp32). A path is live only when the device enables
`shaderFloat16` (`ERHE_HAS_SHADER_FLOAT16`); without it every variant
compiles the fp32 chain whatever the mask, and the mask 0 variant is the
fp32 chain operation for operation as before the axis existed.

The environment key carries the preset's mask (`Forward_renderer` and
`Draw_list_renderer` parameters `fp16_paths`, `Color_environment`,
`Prewarm_parameters`); `set_fp16_paths_axis()` keeps it only on lit color
keys (`BXDF_MODEL` not unlit, `SHADER_DEBUG` 0, none of the position-pass
variant bits) and sets 0 on every other key, applied at the same sites as
`set_specular_anti_aliasing_axis()`. The unlit, depth, shadow, ID, points
and debug variants therefore never multiply; switching the mask compiles
the lit variants of the new mask once. The variant browser codes the axis
`H`.

### Overdraw (`OVERDRAW`)

The integer axis `ERHE_OVERDRAW` is the `erhe::scene_renderer::Overdraw_count`
value (0 off, 1 shaded, 2 rasterized; the `ERHE_OVERDRAW_*` constants of
`erhe_standard_variant.glsl`). A variant with a non-zero value counts:
`erhe_count_overdraw()` (`res/shaders/erhe_overdraw.glsl`) adds 1 with
`atomicAdd` to the pixel's uint in the storage block `overdraw_count`
(binding 13, `overdraw_count_buffer_binding_point`; only the programs whose
`ERHE_OVERDRAW` is not 0 declare it, `Program_interface::make_prototype()`;
the PVS variants' `pvs_layered` block shares the binding, which Metal leaves
as the only free render pipeline buffer index, and no key carries both),
indexed row by row over `camera.cameras[0].viewport`. `standard.frag` under
the axis runs the material inputs and the alpha-test / screen-door discard as
the real variant, writes a constant color and skips the lighting; the points
variant writes its point color.

- shaded counts the invocations the real shader runs, after the discard. A
  variant that cannot discard declares `layout(early_fragment_tests) in;`
  (storage writes would otherwise turn the implicit early depth test off);
  one that can (`ERHE_FRAGMENT_MAY_DISCARD`, derived in
  `erhe_standard_variant.glsl` from the blending mode axis) keeps the late
  tests: the shader runs for every rasterized fragment, as the GPU runs the
  real discarding shader, and counts the ones that survive the discard.
- rasterized counts at the top of `main()`, before any discard, and never
  declares early fragment tests: the shader runs before the depth test, so
  every rasterized fragment counts, whatever the depth buffer holds.

Helper invocations store nothing and never count.

The axis is per pass, not part of the color environment: the bucket path
sets it in `Forward_renderer::prepare()` (`Prepare_parameters::overdraw`)
and `bucket_primitives()` clears it on depth-only and ID keys; the draw-list
path resolves it per list (`Draw_color_parameters::overdraw`, cached beside
the off resolution in `Draw_list_color_resolution::overdraw`), so views that
count and views that do not share a scene's draw lists without invalidating
each other. The pass binds the count buffer through
`Base_render_parameters::overdraw_count_buffer`. The editor's use is
`doc/plans/overdraw_visualization.md`; with the axis 0 no binding or buffer
differs.

The four light-count axes per type are populated from
`compute_light_layer_partition(lights)`, which returns a
`Light_layer_partition { per_type_shadow[4], per_type_nonshadow[4] }`.
`Light_buffer::update_light_buffer` consumes the same partition for
its UBO slot assignment so the shader-side counts and the packed UBO
layout cannot diverge.

In `standard.frag` the spot and point loops evaluate a light (spot
cone, shadow lookup, BXDF) only where its range attenuation is above
zero; `get_range_attenuation` is exactly zero beyond a light's range.
Apple's shader compiler (the Metal backend and MoltenVK, both through
SPIRV-Cross MSL) executes the unrolled loop bodies in full otherwise:
on Bistro (43 lights with 15 m range, 8 shadow-mapped) the early-out
took the frame's GPU time from 123 to 40 ms on Metal and from 108 to
35 ms on MoltenVK, with byte-identical images.

## Light array layout invariant

The shaded lights are one array, `light_array.lights` (a storage block at
binding 9 holding each light's shading record; the shadow-mapped lights'
hot and cold records are `light_shadowed_array` (binding 11) and
`light_shadow_world_array` (binding 12), indexed by shadow record, and the
counts and the rest of the light state are the `light_block` uniform block;
doc/erhe/scene_renderer.md "Light records"), ordered type-major: directional, spot, point. Within each
type's range, shadow-mapped lights occupy the prefix (indices
`0 .. SHADOWMAPPED_COUNT - 1` of the range); non-shadow-mapped lights follow
(indices `SHADOWMAPPED_COUNT .. SHADOWMAPPED_COUNT + NOT_SHADOWMAPPED_COUNT - 1`).

`Light_set::resolve()` assigns the slots in this order and
`Light_buffer::update()` writes them. The shader walks each type as two consecutive loops:

```glsl
for (uint i = light_offset;
     i < light_offset + ERHE_LIGHT_COUNT_<TYPE>_SHADOWMAPPED;
     ++i) {
    // shadow-sampling branch
}
light_offset += ERHE_LIGHT_COUNT_<TYPE>_SHADOWMAPPED;
for (uint i = light_offset;
     i < light_offset + ERHE_LIGHT_COUNT_<TYPE>_NOT_SHADOWMAPPED;
     ++i) {
    // direct branch
}
light_offset += ERHE_LIGHT_COUNT_<TYPE>_NOT_SHADOWMAPPED;
```

Both bounds are literal `#define` integers, so each loop unrolls or
disappears at compile time.

## Macro coexistence

`ERHE_ATTRIBUTE_a_*` and `ERHE_USE_VERTEX_VARYING_*` coexist:

- `ERHE_ATTRIBUTE_a_*` is auto-injected from the active `Vertex_format`
  and gates raw attribute reads (`a_normal`, `a_texcoord_0`, ...).
- `ERHE_USE_VERTEX_VARYING_*` is set by the variant key and gates
  varying declarations (`out vec3 v_N;` etc.) plus the assignments
  that pass values to those varyings.

`Shader_key::derive` guarantees
`ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X`.

## How renderers use the cache

`bucket_primitives()` in `mesh_memory.{hpp,cpp}` walks a mesh span,
groups primitives by buffer set, and -- under the
`require_exact_variant_signature` policy -- by `Shader_key`. Each
resulting `Render_bucket` carries a `Shader_key` plus its hash; the
renderer looks the key up in `Shader_variant_cache::get(...)` once per
bucket and binds the resolved `Shader_stages` for the draw.

- `Forward_renderer::render` uses
  `Variant_signature_policy::require_exact_variant_signature`, so
  buckets split per material / vertex-format / skin-state and each
  one gets the right-sized variant.
- `Shadow_renderer::render` uses
  `Variant_signature_policy::accept_all_variant_signatures`. The
  shadow path runs a depth-only variant
  (`Shader_bool::VARIANT_DEPTH_ONLY`) and intentionally does not
  resolve per material, so one bucket per buffer set is sufficient.

## Adoption status

The variant cache covers every lit / debug path the editor ships.
The legacy stand-alone shaders are folded in as variant axes:

- `depth_only.{vert,frag}` was removed; depth-only is the
  `VARIANT_DEPTH_ONLY` boolean axis.
- `anisotropic_slope` and `anisotropic_engine_ready` joined
  `erhe::primitive::Bxdf_model`; `standard.frag`'s `BXDF_CALL` macro
  picks `slope_brdf` for SLOPE and uses the existing
  `anisotropic_brdf` (with a 1e-7 roughness floor) for ENGINE_READY.
- `circular_brushed_metal` is the `USE_CIRCULAR_BRUSHED_METAL`
  boolean axis driven by `material.use_circular_brushed_metal`.
- The `standard_debug` ERHE_DEBUG_* modes became the
  `Shader_debug` enum stored per `Viewport_scene_view`. The
  `SHADER_DEBUG` count axis selects one variant per `Shader_debug`
  value; `standard.frag` emits an `#if ERHE_SHADER_DEBUG == N`
  override block per value at the end of `main()` that fully
  replaces the lit colour.
- ID rendering, brush preview, and rendertarget paths are
  `VARIANT_ID_RENDER`, `VARIANT_BRUSH_PREVIEW`, and
  `VARIANT_RENDERTARGET` boolean axes. An ID-render key whose
  `MATERIAL_BLENDING_MODE` is `alpha_test` or `screen_door` derives
  `ERHE_VARIANT_ID_RENDER_ALPHA_TEST` (`erhe_standard_variant.glsl`):
  that variant is not a position pass, so it carries the lit varyings
  and runs the material inputs up to the color pass's discard, then
  writes the packed id instead of the lit color. Every other ID-render
  key is a position pass that writes the id for the whole surface. The
  id outputs use locations 19 and 20, which no lit varying uses.
- The potentially visible set bake's layered buffer (`doc/editor/pvs.md`
  "Bake" 4) is drawn with the `VARIANT_PVS_DEPTH` and `VARIANT_PVS_ID`
  boolean axes (either derives `ERHE_VARIANT_PVS`), resolved from a color
  list's primitive key by `Draw_list_scene::draw_pvs_face()`. The vertex
  shader outputs the fragment's entry (the record's `color.x`, the chunk's
  first frame-local entry index, plus `ERHE_PRIMITIVE_INDEX`) and its
  linear view depth (`precise`), at locations 19 and 20 (the variants never
  combine with ID render). The fragment shader writes no color: it computes
  the depth layer (`erhe_pvs_layer.glsl`, whose C++ twin is
  `erhe::scene_renderer::pvs_layer_of_depth()`) and writes the
  `pvs_layered` storage block (`Pvs_layered_interface`; binding 13, declared
  by the PVS variants only and shared with the overdraw count block) with
  atomics -
  `atomicMin` of the depth bits for `VARIANT_PVS_DEPTH`, and for
  `VARIANT_PVS_ID` `atomicMin` / `atomicMax` of the entry where its depth
  bits equal the stored ones; a fragment outside `[z_near, z_far]` writes
  nothing. A PVS key whose `MATERIAL_BLENDING_MODE` is `alpha_test` or
  `screen_door` derives `ERHE_VARIANT_PVS_ALPHA_TEST`, built like the
  alpha-tested ID-render variant: it runs the material inputs up to the
  color pass's discard and writes where a fragment survives. Every other
  PVS key is a position pass.
- Solid wireframe (real polygon edges blended over the lit fill,
  see `doc/editor/rendering.md`) is the `SOLID_WIREFRAME` boolean
  axis, gated on `Device_info::use_solid_wireframe`.

## Hot reload + invalidation

Cached entries are owned by `Shader_variant_cache` for the editor's
lifetime. Each entry registers with `Shader_monitor` so a saved edit
to `standard.{vert,frag}` reloads every active variant in place; the
cached `Reloadable_shader_stages*` stays stable across reloads.
`Shader_monitor::update_once_per_frame` calls
`Device::clear_render_pipeline_cache()` after any reload so the
format-hashed `Render_pipeline` variants in each `Base_render_pipeline`
do not keep referencing the old shader module.

Runtime material edits do not need a per-edit invalidation hook.
`bucket_primitives()` re-derives the `Shader_key` from the live
`Material_data` every frame, so flipping an axis (adding a texture
sampler, switching BxDF model, toggling circular brushed metal) just
routes the affected bucket to a different cache entry on the next
frame -- compiled on first use, hit thereafter. Old entries remain
correct for their (old) keys and may be reused by other materials.

For this to be visible, `Base_render_pipeline::get_pipeline_for` must
key its `Render_pipeline` variants by the full
`(shader_stages, vertex_input, vertex_format, render-pass format)`
tuple, not by render-pass format alone. `compute_format_hash` in
`render_pipeline.cpp` folds the three pointers in. Before that fix,
the first shader variant to draw under a given Base_render_pipeline +
format would lock its pipeline into the cache and every later variant
that hashed to the same format would render with the wrong shader --
which made material edits appear to do nothing.

## Future work

- [plans/shader_fp16.md](../plans/shader_fp16.md): switchable fp16 paths
  through the standard shader, each with a boundary statement and a gate.
