# Lightmap baking plan

Status: IN PROGRESS on branch lightmap-baking (started 2026-08-01).

## Status (2026-08-01)

DONE, committed, verified:
- Phase 1: texcoord channel 2 end-to-end (geometry attributes,
  Mesh_memory vertex formats, Primitive_builder, make_atlas usage_index
  2, v_texcoord_2 varying + "TexCoord 2 (Lightmap)" shader debug mode),
  Item_flags::lightmapped (bit 30, glTF-persisted by name), Lightmap
  window with Generate Lightmap UVs (undoable Make_atlas_operation).
- Phase 2: Lightmap_baker (src/editor/renderers/lightmap_baker.*) -
  atlas layout (area x density, skyline packing, POT page 256..4096,
  4-texel padding) + UV-space texel G-buffer (world position + normal,
  RGBA32F, alpha = coverage) with debug PNG output.
- Phase 3A: direct-light gather - ray-query compute, per-texel explicit
  light sampling (directional/point/spot; light node +Z convention)
  with shadow rays against BLAS/TLAS of ALL content meshes. Verified:
  correct occlusion + contact shadows on the default scene.
- Phase 5: runtime sampling - Mesh_primitive::lightmap_uv_scale_offset
  -> Primitive_buffer vec4 -> flat varying (loc 22); s_lightmap
  (c_texture_heap_slot_lightmap = 4) bound by Light_buffer::
  bind_lightmap (black fallback) from Forward_renderer;
  standard.frag replaces the ambient term when the region is valid.
  USER-VERIFIED in the viewport 2026-08-01.

Test loop (MCP, editor launched hidden): set_item_flags lightmapped ->
select_items -> generate_texture_coordinates texcoord_slot=2 ->
lightmap_update_atlas -> lightmap_bake_gbuffer ->
lightmap_bake_direct (optional debug_png). Or interactively via the
Lightmap window (Generate Lightmap UVs / Update Atlas Layout / Bake
Direct Lighting).

Known interim behavior:
- The bake holds DIRECT light only, and analytic lights still run for
  lightmapped meshes, so direct light doubles up. DECIDED (user,
  2026-08-01): the lightmap will hold full direct+indirect and the
  analytic light loops get gated off per lightmapped draw (same
  v_lightmap_scale_offset gate) - implement the gate as the first step
  of the next session, before/with 3B.
- Bakes are standalone submits (wait_idle); the §3a interactive loop
  replaces them.
- Stale lightmap_uv_scale_offset survives on meshes whose flag is later
  cleared until the next bake; harmless (they just keep sampling).

Gotchas learned:
- Vulkan combined_image_sampler bindings are offset past the max
  buffer binding in a bind group; raw bindings (acceleration
  structure, storage image) are NOT - pick user binding points that
  do not collide after the offset (see lightmap gather layout).
- accelerationStructureEXT must be declared manually in GLSL (like
  ray_trace.comp); samplers / storage images / uniform blocks are
  auto-injected from the bind group layout.
- The lit fragment path has no draw id; per-draw data reaches
  standard.frag as flat varyings (hence loc 22 for the lightmap
  region), not primitive.primitives[] indexing.
- Bake command buffers use thread slot 6 (7 is the texture-graph
  export slot).

Also done: the analytic-light gate (standard.frag skips the light loops
for draws with a valid lightmap region) and phase 4 dilation, both
committed 2026-08-01.

DONE 2026-08-02 - Phase 3B interactive loop (section 3a), verified on the
default scene against the analytic ground truth:
- Accumulation atlas (RGBA32F sum + count) + resolve pass publishing the
  running average into the display atlas, dilated per publish.
- Per-frame budgeted tick (editor.cpp, before rendergraph execute):
  tile-cursor bands of <= 2^18 texels/frame recorded into the frame
  command buffer via ring buffers + per-frame-in-flight TLAS slots; ~1.5
  ms/frame at 4096^2 on the dev RTX GPU (~16 sweeps/s).
- Change-driven invalidation via three FNV hash tiers (lightmapped set +
  density -> re-layout; region transforms -> re-raster G-buffer; lights +
  occluder transforms -> reset accumulation); verified with an MCP
  edit_light call (sweeps 145 -> 1 -> reconverge).
- Indirect bounces: one cosine hemisphere ray per texel per sample;
  radiance = albedo_hit * published_irradiance(hit lightmap UV) via
  per-instance BDA texcoord-2 fetch (Lm_instance_record SSBO), manual
  backface rejection via position fetch. G-buffer gained an albedo
  target (material base-color factor; textures ignored for now) - also
  the future JNLM guide.
- UI: Lightmap window Baking checkbox + Reset + sweep readout; MCP tool
  lightmap_set_baking {enabled, reset, debug_png} toggles/queries/dumps.
- bake_direct() remains the one-shot path (MCP/window), now implemented
  as reset + one full-atlas sample through the same shader.
Known deliberate gaps: no ms-adaptive budget (fixed texel band), no
adaptive bounce-origin bias yet (single bounce spawns only from G-buffer
texels, where the fixed 1 cm bias applies), dynamic occluder motion
resets accumulation every frame (physics jitter would prevent
convergence - revisit if it bites), bake_gbuffer on invalidation is a
standalone wait_idle submit (hitch on lightmapped-mesh transform edits).

Bug found + fixed 2026-08-02 (the "everything is green" report): the
G-buffer fragment shader read lightmap_draw.base_color directly, but the
per-draw UBO binding is declared vertex-stage-only - the fragment-stage
read is undefined and on the dev NVIDIA driver aliased world_from_node
column 1, so every unrotated draw baked albedo (0,1,0) and the bounce
tinted the whole atlas green. Fix: base color rides a vertex->fragment
varying. Diagnostics that isolated it (kept): ERHE_LM_NO_INDIRECT=1 env
var compiles the bounce out (pure-direct atlas), and
debug_write_gbuffer_pngs now also dumps <base>_albedo.png.

OPEN QUESTION for the user (metals): every default-scene material is
metallic = 1. The bake stores diffuse irradiance; the G-buffer albedo is
now base_color x (1 - metallic) so metals bounce ~nothing (physically
right), but standard.frag's runtime term is lightmap * base_color
WITHOUT the (1 - metallic) weight, and the lightmap gate also disables
analytic specular. So lightmapped metals currently render as pastel
diffuse (wrong but visible); weighting by (1 - metallic) would render
them near-black (right for diffuse, but metals live on specular, which
nothing provides). Options: (a) keep as is, (b) add the weight and
accept dark metals until specular handling exists, (c) re-enable
analytic specular (only) for lightmapped draws, (d) bake a specular
approximation later. Needs a user decision.

### Article alignment (user-directed 2026-08-02)

DECISION: follow the Bakery article ("Baking artifact-free lightmaps on
the GPU", key reference #1 in section 1.1) closely. Exceptions:
- We are Vulkan: the denoiser is a Vulkan-native compute pass (Godot's
  JNLM port as planned), NOT the article's OptiX AI denoiser. The
  article's reversible-tonemap trick is OptiX-specific; JNLM works in
  linear HDR directly.
- Mip-level UV chart repacking stays out of scope until the lightmap
  has mips at all.

As-built state the alignment starts from: the committed gather uses a
FIXED 1 cm normal-offset ray bias and shadow rays with backface culling
DISABLED (that combination fixed the observed contact leak), plus fold
cull at raster time and dilation. The article instead keeps culling
sane and prevents leaks at the SOURCE (sample position). The alignment
replaces our ad-hoc defenses with the article's, in this order:

Item 1 DONE 2026-08-02: native conservative rasterization.
VK_EXT_conservative_rasterization (overestimation, properties-only - no
feature struct) detected in query_device_extensions, exposed as
Device_info::use_conservative_rasterization, opt-in per pipeline via
Rasterization_state::conservative_enable (chained
VkPipelineRasterizationConservativeStateCreateInfoEXT in both Vulkan
pipeline-build paths; ignored on GL/Metal/unsupported). Lightmap_baker
sets it and rasters ONE pass (the center tap); the 9-tap jitter loop
remains as the fallback (first_jitter_pass selects). Verified: extension
enabled on the dev NVIDIA GPU, G-buffer log says "native conservative
raster", bake output matches the jitter path in the viewport.

Item 2 DONE 2026-08-02: article leak defenses replace the as-built set.
- Adaptive bias (pos += sign(dir) * max(abs(pos) * 2e-7, 1 micron)) for
  shadow AND bounce ray origins; the fixed 1 cm offset is gone (the UBO
  ray_bias field remains as padding).
- Virtual offset / sample push-off runs as a ONE-SHOT compute pass
  (c_adjust_source, record_adjust()) rewriting the position G-buffer in
  place after every G-buffer bake - first cut ran it per texel per
  sample inside the gather and tripled frame time (29 ms); as a
  pre-pass, steady-state cost is zero (8.3 ms, same as before).
- World texel size rides in G-buffer normal.w (derivative trick at
  raster time, x sqrt(2)).
- Backface-hit invalidation: a bounce ray hitting a backface closer
  than one texel zeroes the texel's accumulation (dilation fills it);
  farther backface hits contribute nothing.
- Shadow rays trace WITH gl_RayFlagsCullBackFacingTrianglesEXT again.
Regression gate passed: torus/capsule/sphere contact shadows intact, no
leaks, no acne, frame time unchanged.

Item 3 DONE 2026-08-02: G-buffer smooth position + terminator fix.
- New 4th G-buffer attachment: Phong-tessellated smooth position
  (rgba32f). Computed per fragment WITHOUT vertex fetch or barycentric
  extensions: smooth(p) = p - (M(p)*p - b(p)) where M = sum w_i n_i
  n_i^T and b = sum w_i dot(p_i,n_i) n_i interpolate linearly as
  varyings and the quadratic term falls out of applying interpolated M
  to interpolated p (derivation in c_vertex_source).
- Face normal is NOT stored (deviation from the article's layout): its
  only consumer is the "smooth position must stay on the front side of
  the face plane" validation, and the fragment shader already has the
  face normal from dFdx/dFdy of world position (oriented by the smooth
  normal), so that validation happens at raster time. Revisit if a
  later phase needs the face normal per texel; this also kept the pass
  within the graphics layer's 4-color-attachment limit.
- The neighbor-geometry validation runs in the one-shot adjust pass
  (which has the TLAS): a segment ray from the flat to the smooth
  position; any hit keeps the flat position. The winner then goes
  through the existing virtual-offset probes and is written into the
  position G-buffer, so the gather is untouched.
- Diagnostics: ERHE_LM_NO_SMOOTH=1 env var compiles the adoption out
  (A/B), and debug_write_gbuffer_pngs logs per-region smooth-vs-flat
  delta stats.
- Verified 2026-08-02: deltas are zero on flat-shaded charts (floor,
  polyhedra) and up to ~5 mm on curved ones (capsule/cylinder/sphere/
  cone/torus, 40-50% of texels); the A/B atlas diff is confined to
  exactly the curved charts (floor bit-identical), strongest on the
  torus. The visible effect at 64 tpm is subtle in the tone-mapped
  atlas - final plateau judgment is a viewport call, and phase 4
  denoise stacks on top of these ray origins.

Item 4 DONE 2026-08-02: JNLM denoise (phase 4 second half).
- c_denoise_source is a port of Godot's lm_compute.glsl MODE_DENOISE
  joint non-local means (MIT, based on YoctoImageDenoiser; attribution
  in the code), guided by G-buffer albedo + normal. Validity comes from
  the published alpha and a zero-length normal (mirrors Godot's normal/
  occlusion masks and naturally excludes backface-invalidated texels).
- Windows are compile-time defines REDUCED from Godot's (half search 3
  vs 10, half patch 1 vs 3) because this runs inside the interactive
  loop, not once at bake end; sigmas are Godot's defaults. A future CLI
  bake can raise the windows.
- Publish cadence changed: during sweep 0 (and after any reset) the raw
  average still publishes every tick for immediate feedback; from sweep
  1 on, publishing happens only on sweep completion as resolve ->
  denoise -> dilate. Mid-sweep ticks skip resolve+dilate entirely, so
  steady-state per-tick cost went DOWN and the viewport (and bounce
  feedback) sample a stable denoised atlas. Denoise writes into the
  dilate scratch; dilation then runs an odd iteration count so the
  final pass still lands in the published texture.
- Verified 2026-08-02: convergence rate unchanged (~1.6 sweeps/s on the
  4096 default-scene atlas), contact-shadow edges preserved (guides
  working), curved charts and bounce glows smooth, viewport clean,
  ~10 ms frame with baking enabled.

Item 5 DONE 2026-08-02: seam blend as a standard per-publish step.
- Seam detection runs on the GEO mesh in build_seam_vertices() (called
  from update_layout), where shared vertex ids make the position test
  exact: a facet edge whose second occurrence carries different
  channel-2 UVs is a seam. Corner normals must match within dot 0.99
  per endpoint (hard edges have genuinely discontinuous lighting and
  are not blended); Godot-style guards for 3+-facet edges. Default
  scene: 1172 seam edges.
- Rendering (reference Godot lm_blendseams MODE_LINES): per publish,
  after denoise+dilate, the published atlas is copied into the dilate
  scratch (Godot's light_accum_tex2 role - avoids the read/write
  hazard), then each seam draws as two atlas-space lines, one per
  side, sampling the OPPOSITE side from the copy at alpha 0.5 with
  standard alpha blending (dst alpha preserved - it is the validity
  flag). Because both directions sample the pre-pass copy, one pass
  equalizes both sides to their average - Godot's depth-mask + eight
  jitter passes are deliberately skipped (dilation already guards the
  bilinear skirt).
- The atlas texture gained color_attachment usage, the dilate scratch
  sampled + transfer_dst; seam vertices ride a vertex ring buffer per
  publish.
- Verified 2026-08-02: an atlas diff against the same bake without the
  pass changes exactly the chart-boundary texels in every region
  (cylinder cap circles, polyhedra face outlines, cube face squares),
  orientation and UV mapping confirmed correct; viewport clean.

Item 6 DONE 2026-08-02: bicubic lightmap sampling in standard.frag.
sample_lightmap_bicubic(): cubic B-spline reconstruction via the
standard 4-bilinear-tap trick, replacing the plain bilinear atlas
sample. The 4x4 footprint reads at most 2 texels outside a chart
(vs bilinear's 1), inside the s_padding = 4 dilation skirt. Verified
in the viewport 2026-08-02.

NEXT (in order):
1. Phase 6 ERHE_lightmap GLB persistence + RGB9E5; later CLI bake.

Goal: bake static scene lighting into a lightmap texture with **minimum
authoring effort** — lightmap UVs are assigned automatically, there is
ideally one user-visible quality knob (texel density), and the bake runs
on the GPU using Vulkan ray query.

The bake is **interactive and iterative**: it runs inside the normal
frame loop under a per-frame GPU budget, the user keeps navigating and
editing the scene while it converges, the viewport always shows the
current (partially converged) lightmap, and edits — moving a light,
changing its color, toggling it — restart accumulation so the lightmap
visibly re-converges toward the new lighting. "Bake" is a mode you
leave on, not a modal job you wait for.

Interactive is the **default and first-built** mode. A later addition
is a **non-interactive mode**: a fully unattended bake runnable from
the command line (load scene → converge to target quality → denoise →
save → exit). The core is built so both modes drive the same bake
machinery (§7).

Related docs: `doc/raytrace-plan.md` (GPU ray trace renderer this builds
on), `doc/uv_editor.md` (UV infrastructure inventory), `doc/shadows.md`,
`doc/scene_serialization.md`.

---

## 1. Research summary

### 1.1 What the modern bakers do (external survey)

The industry has converged on one architecture (Godot 4 LightmapperRD,
Unity Progressive GPU Lightmapper, Unreal GPU Lightmass, Frostbite Flux,
Bakery):

1. **Automatic per-mesh UV unwrap** into a dedicated lightmap channel
   (everyone uses xatlas or an equivalent; Godot vendors xatlas).
2. **Per-instance atlas packing**: instances get a rectangle in a shared
   atlas sized by world-space texel density; runtime UV =
   `uv2 * scale + offset` (+ optional atlas page).
3. **UV-space rasterization** of each chart into a *texel G-buffer*
   (world position, normal, albedo, emissive per lightmap texel), with
   conservative coverage (HW conservative raster or multi-jitter
   re-render) plus dilation.
4. **Progressive path-traced gather** per texel in a compute shader:
   direct light via explicit light sampling (NEE), indirect via
   cosine-weighted hemisphere rays, accumulated over many dispatches so
   the app stays responsive. Hemicube radiosity (The Witness,
   ands/lightmapper.h) is obsolete — slower convergence and no denoiser
   fit.
5. **Artifact hardening** — this is the long tail:
   - light-leak prevention: virtual offset (tangential push-off rays),
     adaptive ray-origin bias (`pos += pos * 2e-7`), backface-hit
     invalidation of texels;
   - guided denoising (Open Image Denoise, or Godot's self-contained
     JNLM compute shader — non-local means guided by albedo+normal);
   - dilation of valid texels into gutters;
   - seam handling: ≥2 texel chart padding + optional least-squares seam
     stitch (ands/seamoptimizer, zlib license, single file).
6. **HDR storage**: bake in float; ship RGB9E5 (4 B/texel, filterable,
   supported everywhere including Quest) or RGBA16F.

Key references (read in this order when implementing):

- Bakery author, "Baking artifact-free lightmaps on the GPU" —
  https://ndotl.wordpress.com/2018/08/29/baking-artifact-free-lightmaps/
  — *the* practical artifact checklist (UV-space raster, multi-offset
  conservative coverage, push-off, bias, dilation, bicubic sampling).
- Godot `modules/lightmapper_rd` (MIT, ~7 files: `lm_raster.glsl`,
  `lm_compute.glsl`, `lm_blendseams.glsl`, JNLM denoiser) — the best
  complete open implementation; GLSL ports nearly verbatim, and its
  software-grid traversal can be swapped for `rayQueryEXT`.
  https://github.com/godotengine/godot/tree/master/modules/lightmapper_rd
- nvpro `vk_mini_path_tracer` — compute + ray query path tracer
  tutorial; essentially our bake shader minus the UV-space G-buffer.
  https://nvpro-samples.github.io/vk_mini_path_tracer/
- MJP BakingLab (MIT) — reference for basis encodings (flat irradiance
  vs HL2 vs SH L1 vs SG). https://github.com/TheRealMJP/BakingLab
- ands/seamoptimizer (least-squares seam fix, drop-in) —
  https://github.com/ands/seamoptimizer
- Castaño's Witness posts (sample validity, parameterization,
  compression) — http://www.ludicon.com/castano/blog/articles/lightmap-parameterization/

Libraries worth integrating directly: **none are needed for the core**
(see below — erhe already has the pieces); candidates for the polish
phase are **seamoptimizer** (single file, zlib) and **OIDN** (Apache 2,
optional external denoiser as Godot does). Standalone **xatlas** (MIT,
2 files) is a fallback if Geogram-based unwrap quality disappoints.
ands/lightmapper.h was evaluated and rejected (OpenGL hemicube
radiosity — wrong architecture for us).

### 1.2 What erhe already has (codebase survey)

The starting position is unusually strong; the baker is mostly a new
dispatch domain over existing machinery.

- **GPU ray tracing is done.** `VK_KHR_acceleration_structure` +
  `ray_query` + `ray_tracing_position_fetch` detected and abstracted
  (`src/erhe/graphics/erhe_graphics/acceleration_structure.hpp`,
  `Device_info::use_ray_query`). `Ray_trace_renderer`
  (`src/editor/renderers/ray_trace_renderer.cpp`) already builds
  BLAS-per-`Buffer_mesh` from the live Mesh_memory GPU pools, a TLAS per
  frame slot, and `res/editor/shaders/ray_trace.comp` already shades
  ray-query hits with real materials, all light types, and traced
  shadow rays, fetching stream-1 attributes via buffer device address.
  It is ray-query-in-compute by design (no RT pipeline / SBT) — exactly
  what a baker wants.
- **Automatic UV unwrap is done.**
  `erhe::geometry::operation::make_atlas()` /
  `generate_mesh_atlas_texture_coordinates()`
  (`src/erhe/geometry/erhe_geometry/operation/make_atlas.cpp`) —
  Geogram parameterizers (LSCM/ABF/…) + xatlas chart packing
  (`GEO::PACK_XATLAS`), writing into a selectable corner texcoord
  channel; already exposed in the operations UI and over MCP.
- **Multi-UV plumbing exists end-to-end**: data-driven vertex formats,
  `tex_coord` usage_index 0 and 1 in stream 1 of every Mesh_memory
  format, `v_texcoord_1` varying + `ERHE_SELECT_TEXCOORD`, per-sampler
  `*_TEX_COORD` shader-key ints. Caveat: **channel 1 is already
  claimed** (glTF `TEXCOORD_1`, circular-brushed-metal, facet coords) —
  the lightmap gets a **new channel 2**.
- Compute encoder with storage images, texture→buffer readback recipe
  (`ray_trace_renderer.cpp:411` `read_output_rgba8`), offscreen
  render-to-texture helper (`src/editor/texture_graph/texture_renderer.cpp`),
  RGBA16F/RGBA32F formats, mipmap generation.
- Shader variant system (`shader_key.hpp` X-macros) and programmatic
  material GPU struct (`material_buffer.cpp`) make the runtime sampling
  hook mechanical; the occlusion-texture sample at
  `res/shaders/standard.frag:452` is the natural sibling.
- Vendored `RectangleBinPack` (Skyline/MaxRects) for per-instance atlas
  packing; mikktspace; CPU raytrace fallback (`erhe::raytrace`:
  embree/bvh/tinybvh) if ever needed.
- Persistence: scenes save as erhe-authored GLB with `ERHE_*` vendor
  extensions and embedded images — a natural home for baked lightmaps.

Gaps to build (nothing exists for these): UV-space texel G-buffer pass,
the bake gather shader (sampling/RNG/accumulation), dilation/denoise/
seams, a static/lightmapped item flag, per-instance lightmap
scale/offset in per-draw data, HDR image writer (fpng is 8-bit PNG
only), and bake caching/invalidation.

---

## 2. Design decisions

- **Bake on Vulkan only**, gated exactly like
  `Ray_trace_renderer::is_supported()` (`Device_info::use_ray_query`).
  Runtime *sampling* of the result works on every backend and on Quest.
- **Ray query in compute**, not an RT pipeline. Reuse
  `ray_trace.comp`'s BLAS/TLAS setup, instance records, BDA attribute
  fetch, and `erhe_light.glsl`/`erhe_bxdf.glsl`.
- **UV channel 2** (`tex_coord` usage_index 2) is the lightmap channel.
  Added to stream 1 of the Mesh_memory vertex formats; channels 0/1
  keep their current meanings.
- **Unwrap with the existing `make_atlas`** (LSCM parameterize + xatlas
  pack) per mesh into normalized 0–1 charts. Standalone xatlas is the
  contingency if chart quality or robustness disappoints (it also
  handles triangle soups that never had a `Geometry`).
- **Per-instance atlasing**: each lightmapped mesh instance gets a
  rectangle in a shared atlas page (RectangleBinPack skyline), sized by
  `surface_area × texels_per_meter`. Per-draw `vec4 uv_scale_offset`
  (+ page index if paging is ever needed) lives in the primitive/draw
  data, not the material — the same material must work lightmapped and
  not.
- **Flat RGB irradiance** first (no directionality). SH L1 is an
  explicit later extension; the storage layout should not preclude it.
- **The lightmap holds FULL lighting (direct + indirect) and analytic
  lights are gated off per lightmapped draw** (user-decided
  2026-08-01). The same per-primitive region gate that enables the
  lightmap sample skips the analytic light loops in standard.frag, so
  a mesh is lit either entirely by its bake or entirely analytically -
  never both. Consequence for interactivity: a light edit shows through
  re-convergence (direct lands after one full atlas sweep, bounces
  follow), not instantly; the runtime cost of lightmapped meshes is
  minimal.
- **Interactive progressive bake**: the gather runs every frame as a
  budgeted compute dispatch (target ~2 ms GPU, tunable), accumulating
  samples per texel across frames. The viewport samples the live
  running-average texture, so the scene converges on screen while the
  user flies around. There is no modal bake; a "Baking" toggle +
  pause/reset controls.
- **Change-driven invalidation, cheapest-first** (see §3a):
  light edits reset only the accumulation; transform/geometry edits of
  static meshes additionally re-raster the texel G-buffer and rebuild
  the TLAS (already per-frame in `Ray_trace_renderer`); adding/removing
  lightmapped meshes or changing texel density redoes atlas layout.
  Bounce feedback (iterate-on-previous-lightmap) makes lighting edits
  propagate gradually even before reset finishes converging — this is a
  feature, not a bug (real-time-radiosity feel).
- **One user knob**: texels per meter (default ~16). Everything else is
  fixed defaults (padding 4 texels, atlas page 2048–4096, sample count
  target, bounce count 3ish via iterate-on-previous-lightmap).
- **Storage**: RGBA16F during development/debug; RGB9E5 as the shipped
  encoding. Persist inside the scene GLB via an `ERHE_lightmap`
  extension (raw payload in a GLB buffer view avoids needing an
  EXR/KTX2 writer; revisit if interchange matters).
- **Static classification**: new `Item_flags` bit (`static` or
  `lightmapped`, bit 30); flags serialize by name so this round-trips.
  Skinned meshes are excluded (they already have no BLAS).

## 3. Architecture

```
                    ┌──────────────────────────────────────────┐
   per mesh         │ Unwrap: make_atlas → texcoord channel 2  │  (CPU, cached)
                    └──────────────┬───────────────────────────┘
                                   │
   per scene bake   ┌──────────────▼───────────────────────────┐
                    │ Atlas layout: pack instance rects,       │  (CPU)
                    │ assign uv_scale_offset per instance      │
                    └──────────────┬───────────────────────────┘
                                   │
                    ┌──────────────▼───────────────────────────┐
                    │ Texel G-buffer: raster charts in UV space│  (GPU raster,
                    │ → position/normal/albedo/emissive/valid  │   multi-jitter
                    └──────────────┬───────────────────────────┘   coverage)
                                   │
                    ┌──────────────▼───────────────────────────┐
                    │ Gather: compute + rayQueryEXT            │  (GPU, progressive,
                    │ NEE direct + cosine hemisphere indirect, │   reuses ray_trace
                    │ virtual offset, bias, backface kill      │   BLAS/TLAS/materials)
                    └──────────────┬───────────────────────────┘
                                   │
                    ┌──────────────▼───────────────────────────┐
                    │ Post: JNLM denoise → dilation → seams    │  (GPU compute
                    └──────────────┬───────────────────────────┘   + optional CPU seam LSQ)
                                   │
                    ┌──────────────▼───────────────────────────┐
                    │ Runtime: standard.frag samples lightmap  │  (all backends,
                    │ via texture heap, uv2*scale+offset,      │   replaces ambient
                    │ added where ambient/occlusion apply      │   for static meshes)
                    └──────────────────────────────────────────┘
```

New code lives in `src/editor/renderers/lightmap_baker.{hpp,cpp}`
(modeled on `ray_trace_renderer.cpp` — copy its BLAS cache, TLAS, bind
group, dispatch, readback patterns) plus shaders
`res/editor/shaders/lightmap_{raster.vert,raster.frag,gather.comp,denoise.comp,dilate.comp}`.

## 3a. Interactive bake loop and invalidation

Per frame, while baking is enabled (and `use_ray_query`):

1. **Collect changes** since last frame and downgrade the bake state to
   the cheapest level that covers them:

   | Change | Response |
   |---|---|
   | Light moved / recolored / toggled, ambient, emissive material edit | reset accumulation (zero sample counts; keep G-buffer, atlas, BLAS) |
   | Static mesh transform changed | re-raster that instance's G-buffer region, TLAS refresh (already per-frame), reset accumulation |
   | Static mesh geometry edited | rebuild BLAS (existing lazy cache), re-unwrap if topology changed, then as above |
   | Mesh added/removed from lightmapped set, texel density changed | redo atlas layout + full G-buffer + reset |
   | Camera motion, dynamic (non-static) object motion | **nothing** — bake input is unaffected |

   Debounce continuous edits (light being dragged): reset at most every
   frame is fine — reset is a cheap clear; the cost model is "converge
   time restarts", which is exactly the expected UX.

2. **Dispatch a budgeted gather slice**: a persistent cursor walks the
   atlas in tiles; each frame traces `rays_per_frame` (adaptive: scale
   by measured dispatch time toward the ms budget, using the existing
   GPU timing infrastructure). Early passes do 1 spp sweeps of the
   whole atlas so the *entire* scene gets a rough answer fast
   (coarse-to-fine), rather than converging tile 0 fully first.

3. **Publish**: resolve running average (sum / count) into the display
   atlas texture the renderer samples. Optionally run the denoiser on
   the published copy every N frames once sample counts pass a
   threshold — never on the accumulation buffer itself, so denoising
   stays a display-side refinement that improves as input converges.

4. **Bounce feedback**: indirect rays sample the *published* atlas at
   hit points (iterate-on-previous-lightmap). With continuous
   dispatching this behaves like progressive radiosity: after a light
   edit, direct light snaps in within a sweep or two and bounces flow
   in over the following seconds.

State kept per bake session: accumulation atlas (RGBA32F sum + count),
published atlas (RGBA16F), texel G-buffer, atlas layout, tile cursor,
per-cause dirty flags. Everything except the published atlas is
transient; persistence (§ Phase 6) snapshots the published atlas when
the user saves.

UI (Lightmap window): Baking on/off, Pause, Reset, texels/meter,
ms-budget slider (advanced), convergence readout (min/avg spp), atlas
debug view.

## 4. Implementation phases

Each phase ends in something visible/testable in the editor.

### Phase 1 — Lightmap UV channel + unwrap pipeline
- Add `tex_coord` usage_index 2 to the Mesh_memory stream-1 vertex
  formats (`mesh_memory.cpp`) and to `Primitive_builder`; add
  `USE_VERTEX_VARYING_TEXCOORD2` shader bool + `v_texcoord_2` varying.
- Add the `static`/`lightmapped` item flag + UI checkbox.
- Bake orchestration entry (editor window `Lightmap`, the Baking
  toggle + texels/meter setting via the config codegen in
  `src/editor/config/definitions/`): for each flagged mesh lacking
  channel-2 UVs, run `generate_mesh_atlas_texture_coordinates` into
  channel 2 (hard-angle defaults), rebuild the primitive.
- Visual check: UV-debug shader mode showing channel 2 charts (or dump
  the atlas layout as PNG).
- Risk to resolve here: meshes imported as `Triangle_soup` (glTF) may
  lack a `Geometry`; either build one for the unwrap or wire standalone
  xatlas for that path.

### Phase 2 — Atlas layout + texel G-buffer
- Instance rect sizing (`area × density`), skyline packing
  (RectangleBinPack), `uv_scale_offset` per instance stored on the mesh
  attachment and uploaded via `primitive_buffer.cpp` per-draw data.
- UV-space raster pass into RGBA32F position + RGBA16F
  normal/albedo/emissive + coverage targets: vertex shader outputs
  `clip = (uv2 * scale + offset) * 2 - 1`, fragment writes world-space
  attributes. Conservative coverage via native
  `VK_EXT_conservative_rasterization` (overestimation) where exposed;
  multi-jitter re-render (~9 offsets, center-last, per the Bakery post)
  as the fallback (article alignment, status section item 1).
- Debug: visualize the G-buffer in the existing debug/texture windows.

### Phase 3 — Gather (the bake)
- Compute shader + `rayQueryEXT` over valid texels; reuse
  `ray_trace.comp`'s TLAS binding, instance records, material SSBO,
  light structures.
- Step 1: direct light only (NEE to every light with traced shadow ray)
  — a lightmap that matches the shadow-mapped raster look validates the
  whole pipeline.
- Step 2: the interactive loop of §3a — budgeted per-frame tile
  dispatch, accumulation buffer (RGBA32F sum + count), publish to the
  display atlas each frame, light-edit → accumulation reset. From this
  point on the bake is already interactive; later steps only improve
  quality.
- Step 3: indirect — cosine-sampled hemisphere, iterate-on-previous-
  lightmap bounces (rays read the published atlas at the hit point's
  channel-2 UV).
- Leak defenses: TARGET is the article set (adaptive magnitude-
  proportional bias, virtual-offset push-off, backface-hit
  invalidation, shadow rays with normal backface culling) - see the
  article-alignment item 2 in the status section. AS BUILT until that
  lands: fixed 1 cm normal-offset bias + shadow rays without backface
  culling + fold cull + dilation.

### Phase 4 — Post-processing
- Dilation compute pass (valid→invalid 8-neighborhood, ~padding
  iterations).
- Denoise: port Godot's JNLM compute shader (MIT, one file, guided by
  the G-buffer albedo+normal — requires adding an albedo target to the
  G-buffer, which today holds only position+normal), applied
  periodically to the published atlas per §3a so partially-converged
  views look clean too. Runs before any seam pass. OIDN stays an
  optional external step if JNLM proves insufficient.
- Seams: a standard pipeline step after denoise (article alignment
  item 5): GPU edge-bleed pass - seam-edge line lists rendered with
  opposite-side UVs, alpha-blended over multiple passes until
  converged (Bakery / Godot lm_blendseams). Chosen over the CPU
  seamoptimizer LSQ pass, which cannot re-run per publish in the
  interactive loop.

### Phase 5 — Runtime sampling
- `uvec2 lightmap_texture` via the texture heap; because the atlas is
  per-scene (not per-material), bind it through the light/per-view
  block or a dedicated slot rather than `material_buffer`.
- `standard.frag`: for draws with a valid `uv_scale_offset`, replace
  the `light_block.ambient_light` term with the lightmap sample
  (bilinear first; bicubic upgrade later), keep analytic
  specular/direct as configured — start simple: lightmap = full
  diffuse (direct+indirect) for static meshes, dynamic lights can be
  layered back per need.
- Shader key: `USE_LIGHTMAP` bool. Works on GL/Vulkan/Metal since
  sampling is plain texture heap usage.

### Phase 6 — Persistence + encoding
- `ERHE_lightmap` glTF extension: atlas payload (RGB9E5 or RGBA16F
  buffer view + dimensions + encoding), per-instance
  `uv_scale_offset`, per-mesh record that channel-2 UVs are baked
  (they already ride in the `ERHE_geometry` dump / `TEXCOORD_2`).
- Invalidation: store a hash of (geometry ids, transforms, lights,
  density) with the bake; stale bakes still load but are flagged in
  the UI.
- Quest: RGB9E5 sampling is universally supported; the baked scene GLB
  just works. (Bake itself remains desktop-Vulkan.)

## 5. Effort estimate

| Phase | Estimate |
|---|---|
| 1 UVs + flag | ~1 week |
| 2 atlas + G-buffer | ~1 week |
| 3 gather | 2–3 weeks to correct images |
| 4 post | 1–2 weeks (denoise dominates) |
| 5 runtime | a few days |
| 6 persistence | ~1 week |

The artifact-hardening tail (leaks, seams, denoise tuning) is the known
schedule risk everywhere in the literature; the Bakery checklist and
Godot's module are the map for it.

## 6. Requirements the interactive build must not paint over

To keep the later CLI mode cheap, the core observes these rules from
phase 1:

- The bake core (`Lightmap_baker`: atlas layout, G-buffer pass, gather
  scheduling, post passes, persistence) must not depend on the editor
  UI, ImGui, windows, or input — the Lightmap window is a thin client.
  Model: `Ray_trace_renderer` (core) vs `Ray_trace_window` (view).
- The per-frame budget is a parameter where "unbounded" is a valid
  value (dispatch until done, no publish cadence needed).
- Convergence is measurable: the scheduler exposes min/avg samples per
  texel (already wanted for the UI readout) so "converged" can be a
  termination criterion, not just a progress bar.
- Invalidation (§3a step 1) is an optional input source; with no editor
  driving it, the bake is a straight run to convergence.

## 7. Future: non-interactive command-line bake

Later addition, not part of the initial phases. An unattended bake:
load scene GLB → unwrap/atlas as needed → run the gather to a target
quality (min spp and/or variance threshold) at full GPU throughput →
denoise/dilate/seam-fix → write the baked scene GLB → exit nonzero on
failure (no ray-query device, unpackable atlas, …).

Hosting: the existing headless build already runs the engine without a
window/swapchain, which is exactly the environment this needs; the CLI
entry point is a command-line switch (e.g. `--bake-lightmaps
<in.glb> [-o out.glb] [--texels-per-meter N] [--target-spp N]`) that
skips editor UI construction and drives `Lightmap_baker` in a loop
until the convergence criterion is met. Because the interactive mode's
core is UI-free (§6), this phase is mostly argument parsing, progress
logging to stdout, and a saturating (unbudgeted) dispatch loop.

## 8. Out of scope (explicit, for now)

- Directional lightmaps (SH L1) — designed-for but not built.
- Light probes for dynamic objects (Godot pairs these with lightmaps;
  natural follow-up, same gather shader).
- RT pipeline / SBT, GPU denoiser via OIDN in-process, KTX2/EXR
  interchange export, lightmap block compression (BC6H/ASTC-HDR),
  skinned/dynamic geometry, non-Vulkan bake fallback (CPU embree path
  possible later via `erhe::raytrace`).

## 9. Spatial tiling, bake-to-disk, and streaming (2026-08-05)

Supersedes the single-page atlas (§2/§3) and delivers Phase 6
persistence in per-tile form. Goal: baking and rendering always work
with bounded memory, regardless of world extents, mesh count, or
vertex density.

- **Spatial partition** (`Lightmap_baker::update_layout`): regions are
  assigned whole (by world-AABB XZ center) to spatial tiles via a
  recursive area-weighted kd-median split of the XZ plane. Each tile
  packs its regions (skyline, big-first) into one
  `tile_texture_size`^2 texture (config, default 2048, pow2 256..8192).
  Guarantees: tiles split until content fits; co-located sets that
  cannot split spatially split by count into overlapping "overflow
  tiles"; a single region denser than a tile flexes that tile's texel
  density down (warning). Layout can only fail with zero regions.
- **Region addressing**: `Instance_region` rects and `uv_scale_offset`
  are tile-local. The renderer-facing mapping goes through the tile's
  *display slot* (`Atlas_layout::display_uv_scale_offset`); the display
  atlas is a grid of `ceil(sqrt(resident_tile_budget))`^2 tile-sized
  slots (default 9 -> 3x3), so the forward renderer still samples one
  plain 2D texture - zero shader changes. Non-resident tiles publish
  zero scale/offset (the existing no-lightmap gate) and render unlit.
- **Interactive residency**: the tick ranks tiles by camera distance to
  their world bounds each frame and hands the display slots to the N
  nearest; evicted tiles release their fp32 accumulation and zero
  their regions. Bounce feedback across non-resident tiles reads black
  (accepted bias; future: low-res per-tile radiance cache).
- **Bake to disk** (`start_offline_bake` / `offline_tick`, Lightmap
  window "Bake To Disk"): one tile per frame - G-buffer raster,
  `offline_sweeps` full-tile gather submits, resolve/denoise/dilate/
  seam-blend, CPU readback, then `Lightmap_tile_io` writes
  `<scene>.lightmap/tile_<id>.lmt` (64-byte ELMT header + raw RGBA16F)
  and rewrites `manifest.json` (version, tile size, density,
  bake-parameter hash, world bounds + region table per tile). Only one
  tile's working set is ever resident.
- **Streaming** (`Lightmap_streamer`): when the baker does not own the
  lightmap binding, the streamer keeps the `resident_tile_budget`
  nearest tiles resident in its own slot atlas (worker-thread file
  read, staging-buffer upload, <=1 load per frame, eviction hysteresis
  = 1/4 of the incoming tile's XZ extent) and remaps
  `Mesh_primitive::lightmap_uv_scale_offset` per residency change.
  Region identity across reloads is node path + mesh name + primitive
  index; renames orphan regions (unlit, never corrupt). A
  bake-parameter hash mismatch flags the set stale in the Lightmap
  window's Problems list.
- **Failure surfacing** (`Lightmap_report`, App_context): UV unwrap
  exceptions (per-mesh catch + automatic per-facet retry in
  `Make_atlas_operation`), layout warnings (density flex, overflow
  tiles, budget clamps), bake/persist/stream errors all render as the
  red/yellow "Problems" list in the Lightmap window.

## 10. World-space tile partitioning (2026-08-05)

Requirement: bake ALL lightmap-enabled meshes in world space; every
mesh/primitive instance is unique after baking; meshes split into the
spatial tiles by clipping triangles at the tile boundary planes with all
vertex attributes interpolated; clip results shared binary exact across
the two tiles of a plane; originals kept in memory so clipping can be
redone when parameters change.

- **Clipper** (`erhe::geometry::operation::clip_by_tile_tree`,
  `src/erhe/geometry/erhe_geometry/operation/clip_tile_tree.{hpp,cpp}`):
  clips a world-space geometry once down the baker's kd tile tree
  (`Clip_tree_node`: axis 0 = X / YZ plane, axis 2 = Z / XY plane,
  axis -1 = overflow split routed by the pre-assigned tile). Each plane
  is applied to each fan-triangulated fragment exactly once; cuts are
  memoized per (canonical edge, tree node) so both sides reference one
  record. Emission goes through a `Geometry_operation` subclass whose
  identical ordered source lists make `interpolate_mesh_attributes()`
  produce bitwise-identical positions and attributes in both pieces
  (asserted by `test/test_clip_tile_tree.cpp` with memcmp). A vertex
  exactly on a plane is emitted to both sides unmodified. Facet
  provenance is kept as facet attribute "clip_source_facet".
- **kd tree recording**: `update_layout` now emits
  `Atlas_layout::kd_nodes` with explicit plane values (midpoint between
  the sorted centers adjacent to the median split); pack-failure
  re-splits extend the tree (spatial when the halves separate on X,
  overflow otherwise).
- **Partitioner** (`Lightmap_partitioner`,
  `src/editor/renderers/lightmap_partitioner.{hpp,cpp}`): per
  lightmapped mesh/primitive instance: `get_geometry()` ->
  `bake_transform(world_from_node)` -> `clip_by_tile_tree` -> per-piece
  `make_atlas` (usage 2, world-space density, per-facet fallback) ->
  new `Primitive` (renderable + raytrace). Pieces become Mesh_primitives
  of one mesh per source mesh on identity nodes under the
  "Lightmap Pieces" group (world-space vertices, identity transform -
  renderer/raytrace/shadows untouched). Originals stay in the scene;
  the store keeps their primitives for revert / re-prepare. Not routed
  through the undo stack (derived bake artifact). UI: Lightmap window
  "Prepare World-Space Tiles" / "Revert Tiling"; MCP:
  `lightmap_prepare_tiles` / `lightmap_revert_tiles` /
  `lightmap_set_render`.
- **Partitioned layout**: with a prepared partition, `update_layout`
  derives regions from the piece meshes with their pre-assigned tiles
  (no kd re-split; the tile tree is the stored partition). Packing can
  only density-flex (tiles are fixed); pieces that cannot fit even at
  minimum density are dropped with an error.
- **Render toggle + white fallback**: "Render with lightmaps"
  (persisted `lightmap.render_with_lightmaps`) flips visibility between
  originals and pieces. In partitioned mode a non-resident region
  publishes the sentinel `vec4(-1,0,0,0)`; `standard.frag` renders it
  as flat white ambient with analytic lights still gated off, so every
  lightmapped piece keeps rendering until its tile loads. Bounce
  feedback clamps the sentinel to zero (white would inject fake
  energy).
- **Manifest v2 identity**: regions store node path + node index path
  (root-to-node child indices - unique under duplicate names; fixes the
  streamer collision) + mesh name + primitive index + piece_ordinal.
  Pieces store the SOURCE mesh identity; the streamer resolves them
  through the live partition (`Lightmap_partitioner::find_piece`) and
  warns when a piece manifest streams without a prepared partition.
  Evicted pieces publish the white-fallback sentinel.
- **Accepted limitations**: cross-tile cut boundaries are genuine
  lightmap seams (independent bakes/dilation; positions are crack-free,
  only shading discontinuity remains). Exactness holds within one mesh;
  pre-existing inter-mesh cracks are unchanged. Skinned meshes stay
  excluded. Moving a source mesh after the clip leaves stale pieces
  (window warns; re-prepare). prepare() is blocking (main thread);
  async + cancel is future work. N-gon facets are fan-triangulated by
  the clipper even when unclipped.
