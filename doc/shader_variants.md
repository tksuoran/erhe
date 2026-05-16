# Shader variants for editor rendering -- requirements (in progress)

> **Status (2026-05-19): types renamed; design intact.** The variant cache
> shipped, then was renamed and trimmed. Read this document for the design
> motivation, but use these names when reading the current source:
>
> - `Standard_variant_key` -> `Shader_key`
>   (`src/erhe/scene_renderer/erhe_scene_renderer/shader_key.hpp`).
>   Bool axes are an enum-indexed `uint32_t` bit mask (`Shader_bool`); integer
>   axes are a fixed-size `uint32_t[Shader_int::count]` array. The X-macro
>   list (`ERHE_SHADER_BOOL` / `ERHE_SHADER_INT`) is the single source.
> - `compute_standard_variant_key()` -> `Shader_key::derive(material,
>   vertex_format, mesh_has_skin)`.
> - `Standard_shader_variants` is gone. `Shader_variant_cache` in
>   `shader_variant_cache.{hpp,cpp}` keys `Reloadable_shader_stages` directly
>   on `Shader_key`; the old separate variants object and its bucket-level
>   cache are folded into the per-`Render_bucket` `shader_key` /
>   `shader_key_hash` fields.
> - Shadow rendering uses
>   `Variant_signature_policy::accept_all_variant_signatures`; the forward
>   path uses `require_exact_variant_signature` to call
>   `Shader_key::derive()` per primitive.
> - The legacy `ERHE_LIGHT_<TYPE>_COUNT` / `ERHE_LIGHT_<TYPE>_SHADOWMAPPED_COUNT`
>   pair was replaced by a single per-bucket
>   `ERHE_LIGHT_COUNT_<TYPE>_{SHADOWMAPPED,NOT_SHADOWMAPPED}` pair driven by
>   a running `light_offset` cursor in `standard.frag`.
> - Light tallying duplication (the "must stay bit-identical" comment) is
>   gone: `compute_light_layer_partition()` in `shader_key.cpp` is the single
>   source.
> - The depth-only pass is now a `VARIANT_DEPTH_ONLY`-defined variant of the standard
>   shader; `res/shaders/depth_only.{vert,frag}` were deleted.

## Status

**Infrastructure landed; render-time adoption follow-up.** The mesh-memory
refactor (per-format pools + Vertex_format_key registry, lazy buffer
growth, Buffer_range::buffer for renderer-side identity, bucket-by-buffer-set
draw scheduling) shipped in commits 88296e56..04c956fa, so the per-mesh
vertex format axes of the variant key are no longer architecturally
degenerate. Subsequent commits land the shader-side machinery:

- Light buffer partition: each per-type sub-array carries shadow-mapped
  lights as the prefix and non-shadow lights as the suffix; standard.frag
  uses six count-based loops.
- Standard_variant_key (5 material booleans + 6 mesh booleans + 6 scene
  integer counts) and the GLSL define emitter
  (`make_standard_variant_defines`) live in
  `src/erhe/scene_renderer/erhe_scene_renderer/standard_shader_variant.{hpp,cpp}`.
- `Standard_shader_variants` (`standard_shader_variants.{hpp,cpp}`) caches
  compiled variants keyed by `Standard_variant_key`, registers each
  variant with the `Shader_monitor` for hot reload, and supports
  invalidate-all via `clear()`. `Shader_monitor::remove()` was added to
  detach evicted variants safely.
- `compute_standard_variant_key()` reads a (Material*, Vertex_format,
  mesh_has_skin, light counts) tuple into the key, honoring the plan
  invariant `ERHE_USE_VERTEX_VARYING_X => ERHE_ATTRIBUTE_a_X`.
- `res/shaders/standard.{vert,frag}` consume the new macros (gated via
  `res/shaders/erhe_standard_variant.glsl` so the non-variant
  `programs.standard` build path stays bit-identical).
- The editor instantiates the cache on `App_context::standard_shader_variants`
  after `Programs::load_programs` completes.

**Adoption status (B14 closed):** the `standard` shader's variant cache
covers every lit / debug path the editor ships. The legacy stand-alone
shaders (`anisotropic_slope`, `anisotropic_engine_ready`,
`circular_brushed_metal`, `standard_debug`) were deleted and their
features became `standard` variant axes:

- `anisotropic_slope` and `anisotropic_engine_ready` joined
  `erhe::primitive::Bxdf_model`; the `BXDF_CALL` macro in
  `standard.frag` picks `slope_brdf` for SLOPE and uses the existing
  `anisotropic_brdf` (with a 1e-7 roughness floor) for ENGINE_READY.
- `circular_brushed_metal` is the existing `ERHE_USE_CIRCULAR_BRUSHED_METAL`
  boolean axis driven by `material.use_circular_brushed_metal`.
- The 28 `standard_debug` ERHE_DEBUG_* modes became a new
  `erhe::scene_renderer::Shader_debug` enum stored per
  `Viewport_scene_view`. The new `SHADER_DEBUG` count axis on
  `Standard_variant_key` selects one of 29 (`none` + 28) variants;
  `standard.frag` emits an `#if ERHE_SHADER_DEBUG == N` override block
  per value at the end of `main()` that fully replaces the lit colour.

## Original status (now historical)

Implementation was originally blocked on the planned mesh-memory refactor
that lets different meshes carry different vertex formats; until that
landed, every mesh shared a single fixed vertex format and the mesh-side
axes of the variant key collapsed to a constant. That refactor has now
shipped (see above).

## Motivation

Today the editor builds **one** `standard` shader at startup
(`src/editor/renderers/programs.cpp:160`). Every fragment of the lit shader
runs the full code path: texture samplers it does not use, skinning math
it does not need, light loops sized for `max_light_count` even when zero
lights are present.

Vertex attribute presence is already auto-gated via `ERHE_ATTRIBUTE_a_*`
defines injected from the active `Vertex_format`
(`res/shaders/standard.vert:5-95`). The remaining axes (texture presence,
skinning, light counts, varying selection) are not gated, and are silently
paid for by every draw.

Goal: a per-(material, mesh, scene) shader variant cache that compiles the
right-sized lit shader on demand.

## Variant key dimensions

The variant key combines three sub-keys.

### Material sub-key (boolean axes)

| Macro | Set when |
|-------|----------|
| `ERHE_USE_BASE_COLOR_TEXTURE` | `material.data.texture_samplers.base_color.texture != nullptr` |
| `ERHE_USE_METALLIC_ROUGHNESS_TEXTURE` | same for `metallic_roughness` |
| `ERHE_USE_NORMAL_TEXTURE` | same for `normal` |
| `ERHE_USE_OCCLUSION_TEXTURE` | same for `occlusion` |
| `ERHE_USE_EMISSION_TEXTURE` | same for `emissive` |

### Mesh sub-key (boolean axes)

The mesh-side axes are a function of the mesh's `Vertex_format` *and* what
the bound material consumes. The point of the cache: if a material only
samples `base_color` (no normal map, no aniso), the variant should not
declare or pass through tangent/bitangent varyings even when the mesh has
them, and vice versa -- if the material wants a normal map but the mesh
lacks tangents, the variant must drop the normal-mapping path.

| Macro | Set when |
|-------|----------|
| `ERHE_USE_SKINNING` | vertex format has `joint_indices` AND `joint_weights` AND the mesh has a `Skin` attached |
| `ERHE_USE_VERTEX_VARYING_NORMAL` | vertex format has `normal` AND `!material.unlit` |
| `ERHE_USE_VERTEX_VARYING_TANGENT` | vertex format has `tangent` AND `normal` AND (material has a `normal` sampler OR `material.use_circular_brushed_metal`) |
| `ERHE_USE_VERTEX_VARYING_BITANGENT` | same condition as tangent |
| `ERHE_USE_VERTEX_VARYING_TEXCOORD0` | vertex format has `tex_coord,0` AND (any material sampler with `tex_coord==0` is present OR `material.use_circular_brushed_metal`) |
| `ERHE_USE_VERTEX_VARYING_COLOR` | vertex format has `color,0` (always pass through; gating left for later) |
| `ERHE_USE_VERTEX_VARYING_ANISO_CONTROL` | vertex format has `custom,custom_attribute_aniso_control` AND `material.use_aniso_control` |
| `ERHE_USE_CIRCULAR_BRUSHED_METAL` | `material.use_circular_brushed_metal` |

These cannot be derived from material alone -- they require knowing each
mesh's vertex format. **This is the dimension that is currently degenerate
because all meshes share one mesh memory; it becomes load-bearing once
the mesh-memory refactor enables per-mesh formats.**

The variant computer must guarantee `ERHE_USE_VERTEX_VARYING_X =>
ERHE_ATTRIBUTE_a_X` so the existing attribute-presence gating in shader
source remains correct.

### Scene sub-key (integer counts)

| Macro | Source |
|-------|--------|
| `ERHE_LIGHT_DIRECTIONAL_COUNT` | total directional lights this frame (matches existing UBO `directional_light_count`) |
| `ERHE_LIGHT_DIRECTIONAL_SHADOWMAPPED_COUNT` | size of the shadow-mapped prefix in the directional array |
| `ERHE_LIGHT_SPOT_COUNT` | total spot lights |
| `ERHE_LIGHT_SPOT_SHADOWMAPPED_COUNT` | shadow-mapped prefix size for spots |
| `ERHE_LIGHT_POINT_COUNT` | total point lights |
| `ERHE_LIGHT_POINT_SHADOWMAPPED_COUNT` | shadow-mapped prefix size for points |

Counts are **exact** (one variant per observed tuple). No tier rounding.

## Light array layout invariant

There is one light array per type in the lights UBO. Within each per-type
array, shadow-mapped lights occupy the prefix (indices `0 ..
SHADOWMAPPED_COUNT - 1`); non-shadow-mapped lights follow (indices
`SHADOWMAPPED_COUNT .. COUNT - 1`).

`Light_buffer::update()` must enforce this ordering when packing the UBO.
Today's implementation
(`src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp:161-168`)
assigns shadow indices in iteration order but does not partition the array
itself; a partition pass is required.

Shaders loop a type as a pair of consecutive loops:

```
for (uint i = 0u; i < ERHE_LIGHT_<TYPE>_SHADOWMAPPED_COUNT; ++i) {
    // shadow-sampling branch, indices 0 .. SHADOWMAPPED_COUNT - 1
}
for (uint i = ERHE_LIGHT_<TYPE>_SHADOWMAPPED_COUNT;
     i < ERHE_LIGHT_<TYPE>_COUNT; ++i) {
    // direct branch, indices SHADOWMAPPED_COUNT .. COUNT - 1
}
```

Both bounds are literal `#define` integers, so each loop unrolls or
disappears at compile time.

## Macro coexistence

`ERHE_ATTRIBUTE_a_*` and `ERHE_USE_VERTEX_VARYING_*` coexist:

- `ERHE_ATTRIBUTE_a_*` is auto-injected from `Vertex_format` and gates raw
  attribute reads (`a_normal`, `a_texcoord_0`, ...). Unchanged.
- `ERHE_USE_VERTEX_VARYING_*` is set by the variant key and gates varying
  declarations (`out vec3 v_N;` etc.) plus the assignments that pass values
  to those varyings.

The variant computer guarantees `ERHE_USE_VERTEX_VARYING_X =>
ERHE_ATTRIBUTE_a_X`.

## Compilation strategy

- **Async on miss with fallback.** On a render-thread cache miss, queue a
  background compile (taskflow, the same `tf::Executor` already used by
  `Programs::load_programs`) and return the `error` shader for that draw.
  Subsequent frames pick up the compiled variant.
- **GL-thread risk to validate during implementation.** If
  `glCompileShader` / `glLinkProgram` cannot run off the GL context
  thread, dispatch onto a per-frame "compile this frame" queue on the
  main thread instead. Cache structure unchanged; only the dispatch site
  moves.
- **Hot reload preserved.** Each cached variant is a
  `Reloadable_shader_stages` registered with the `Shader_monitor`, so
  edits to `standard.vert` / `standard.frag` reload every active variant.

## Cache invalidation

Triggers that must drop cache entries (initial implementation:
invalidate-all; per-key dependency tracking is a later refinement if
profiling shows hitches):

- Material added or removed from the `Content_library` (already a
  cache-invalidating event for the per-type material list).
- Material edited in the inspector (texture swapped, `unlit` toggled,
  ...).
- Mesh's vertex format changes (only meaningful post-refactor).
- Light count change in any of the six count axes.

Invalidation just empties the table; the next render reissues compiles
for the entries it still needs.

## Axis declaration

Variant axes live as a single C++ X-macro list (one source of truth) used
by:

- the key struct (bit positions for booleans, struct fields for counts),
- the define emitter that produces the `defines` vector for
  `Shader_stages_create_info`,
- any future debug UI that wants to display the active set.

No JSON, no codegen. The axes are part of the shader source contract --
adding one requires editing the shader anyway, so a single shared header
is the right granularity. JSON would be a fourth synchronized location
without decoupling anything.

## Out of scope

- Disk caching of compiled SPIR-V / GL program binaries across editor
  runs.
- Variant cache for non-`standard` shaders (`id`, `depth`, ...).
  `standard_debug` no longer exists -- its modes are SHADER_DEBUG
  values on the standard cache key (see Adoption status above).
- Per-key dependency tracking inside the invalidation path.
- Multiview pairing details (the cache must mirror
  `Programs::load_programs`' `enable_multiview()` pattern, but the
  specifics are an implementation concern).
- Detailed file lists, function signatures, and verification steps --
  those belong in the implementation plan that supersedes this document
  once mesh-memory refactor unblocks the work.

## Prerequisite

Mesh-memory refactor that supports per-mesh vertex formats. Until that
lands, the mesh sub-key is degenerate (constant for all meshes) and the
variant cache reduces to a material-and-scene cache that adds machinery
without paying back enough -- the per-mesh attribute trimming is the main
win. Revisit this document when the refactor is in flight.
