# Shader variants for editor rendering

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
| `ERHE_USE_VERTEX_VARYING_TANGENT` | vertex format has `tangent` AND `normal` AND (material has a `normal` sampler OR the BxDF is anisotropic OR `material.use_circular_brushed_metal`) |
| `ERHE_USE_VERTEX_VARYING_BITANGENT` | same condition as tangent |
| `ERHE_USE_VERTEX_VARYING_TEXCOORD0` | vertex format has `tex_coord,0` AND (any material sampler with `tex_coord==0` is present OR `material.use_circular_brushed_metal`) |
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

The four light-count axes per type are populated from
`compute_light_layer_partition(lights)`, which returns a
`Light_layer_partition { per_type_shadow[4], per_type_nonshadow[4] }`.
`Light_buffer::update_light_buffer` consumes the same partition for
its UBO slot assignment so the shader-side counts and the packed UBO
layout cannot diverge.

## Light array layout invariant

There is one light array per type in the lights UBO. Within each
per-type array, shadow-mapped lights occupy the prefix (indices
`0 .. SHADOWMAPPED_COUNT - 1`); non-shadow-mapped lights follow
(indices `SHADOWMAPPED_COUNT .. SHADOWMAPPED_COUNT + NOT_SHADOWMAPPED_COUNT - 1`).

`Light_buffer::update_light_buffer()` enforces this ordering when
packing the UBO. The shader walks each type as two consecutive loops:

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
  `SHADER_DEBUG` count axis selects one of 32 (`none` + 31)
  variants; `standard.frag` emits an `#if ERHE_SHADER_DEBUG == N`
  override block per value at the end of `main()` that fully
  replaces the lit colour.
- ID rendering, brush preview, and rendertarget paths are
  `VARIANT_ID_RENDER`, `VARIANT_BRUSH_PREVIEW`, and
  `VARIANT_RENDERTARGET` boolean axes.

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
