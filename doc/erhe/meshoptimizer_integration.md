# meshoptimizer integration

Stability: stable

The mesh-optimization subsystem is built on
[zeux/meshoptimizer](https://github.com/zeux/meshoptimizer) (vanilla
upstream, pinned via CPM). `optimize_meshes` defaults to **true** in
`config/editor/mesh_memory.json`; `mesh_optimize_cache` defaults to
**false**.

## Requirements

User-confirmed; the first is a hard requirement.

1. **Source vs optimized asset separation.** Source assets are loaded as-is
   and never mutated. Rendering uses a derived optimized build; export,
   ground truth, geometry reconstruction, CPU raytrace and physics collision
   always use source data at full precision.
2. Optimize **all renderable meshes** -- imported and procedural: vertex
   weld/remap, vertex-cache order, overdraw order, vertex-fetch order, with
   before/after `meshopt_analyze*` statistics. LOD and meshlets are
   explicitly future work -- seams left, nothing implemented.
3. Config: `optimize_meshes` and `mesh_optimize_cache` in the code-generated
   `Mesh_memory_config`. `optimize_meshes` is on by default in the editor
   config; the cache is off. The filesystem cache must work on Android/Quest.
4. **Shared primitives are optimized once, as one** -- glTF import dedups
   `Primitive` slots and brushes share `Primitive` objects; instances reuse
   the shared variant.
5. **Picking and ID rendering stay unconditionally correct**: the `original`
   (source-order, per-corner) build is always present; the optimized build
   cannot serve ID rendering even by mistake (it lacks the facet-id
   attribute entirely). The per-corner facet-id attribute (`a_custom_0`)
   has exactly one consumer, the ID renderer, which uses the always-built
   original variant.
6. Live mesh edits (paint, weight paint, vertex drag) keep working: the
   optimized build is invalidated at edit start, never written through.
7. Vertex-position quantization is routed through `meshopt_quantizeSnorm`.
8. That encode is bit-identical to the `float_to_snorm16` encoder, because
   both encode sites pre-clamp to [-1, 1], where the two agree.
9. The unoptimized **base** variant never uses position quantization:
   full-float positions. In the editor, every primitive always maintains
   this variant, so it is always renderable -- and in-place GPU edits can
   express any position (no AABB clamp, ever).
10. The **optimized** variant is the only build that applies quantization.
    Because the base variant is always available, the optimized variant can
    be built lazily on request, in the background.
11. During a mesh edit operation (paint, weight paint, active mesh-component
    move) the optimized variant is invalidated as soon as the edit starts,
    re-optimization is blocked for the duration of the edit, and at edit end
    an optimization task can start on a background thread.

## Design

### Where the variants live: `Primitive` (settled -- do not re-litigate)

`Primitive` holds the source `Primitive_render_shape` plus one
`shared_ptr<Primitive_render_shape>` per extra variant
(`optimized_render_shape`; null when optimization is off). The optimized
variant is an ordinary geometry-less render shape -- a category that already
existed and was in use -- holding its own triangle soup, its own composed
`Element_mappings`, and its own `Buffer_mesh`.

Two alternatives are rejected:
parallel variant arrays inside `Primitive_shape` / `Primitive_render_shape`
(pays for the second slot in every primitive whether or not the feature is
on, and pairs mesh with mappings by matching array index instead of by
ownership) and variant sets on `erhe::scene::Mesh` (duplicates per-instance
state -- `material`, `lightmap_uv_scale_offset` -- for a choice that is
per-*pass*, never per-instance, and puts the draw-list key
`(object_index, mesh_primitive_index)` at risk). `Primitive` is the only
placement where the mappings live in the same object as the mesh they
describe *and* the off-cost is one null pointer *and* `Mesh` /
`Mesh_primitive` / the draw-list key stay untouched. `Primitive` is also
already the sharing/dedup unit, which is what satisfies requirement 4
directly.

Consequences designed against:

- **Cross-variant publication is not one mutex.** Attach/publish the
  optimized shape only *after* the source commit succeeds; on invalidate,
  detach the optimized shape *first*. A renderer never sees a stale
  optimized mesh beside a committed new source one.
- The optimized shape **never builds its own `Geometry` or raytrace** --
  nothing reaches it to try: every geometry, raytrace, collision and export
  site goes through `primitive.render_shape` / `get_shape_for_raytrace()`,
  neither variant-selected. BLAS (scene TLAS, lightmap baker) builds from
  the **source** shape, which is what removes any BLAS-eviction-on-edit
  requirement.
- **First-optimization-wins** publish-once discipline (two loader workers
  may race on the same shared primitive).
- `Primitive::optimized_render_shape` has **no lock of its own**: every
  writer is either the serial import build loop or a main-thread commit
  (the member comment says so).

### The optimized variant's properties

- **Fill triangles only.** Edge lines, corner points, centroid points and
  the expanded solid-wireframe copy exist per-facet precisely to keep
  corners apart -- which is what welding undoes. The original build carries
  them all.
- **Its own vertex format, without the facet id and with the only quantized
  position** (`Buffer_info::optimized_vertex_format`). The id is per facet
  and the build is welded, so no value describes a merged vertex. Dropping
  the attribute (a) makes the variant unusable for ID rendering by
  construction rather than by convention, (b) takes the id out of the weld
  compare -- leaving it in merges *nothing*, which is the whole win -- and
  (c) saves 4 bytes per vertex. The position is stored in
  `Mesh_memory::optimized_position_format` -- snorm16x3 when
  `quantize_vertex_positions` is on and the device supports it as vertex
  input, float3 otherwise; the content (base) formats always store float3
  (requirement 9), so quantization has no effect unless `optimize_meshes`
  is on. **Both paths (soup and geometry) build in that format, or it is
  not an invariant.** `Mesh_memory` derives the optimized formats from the
  content ones in its constructor (facet-id drop + position substitution);
  `get_all_vertex_formats()` is the single list that repack, lockstep block
  sizing and vertex-input registration all read.
- **`get_resolved_renderable_mesh()` takes the `Primitive_mode`** and
  prefers the optimized build for fill modes only. An empty index range
  reads to every caller as "nothing to draw", not "ask the other build",
  so preferring the variant for an edge-line pass would drop the primitive
  instead of falling back.
- **Every selection point is explicit**: there are no no-arg mesh/mappings
  accessors, so the compiler enumerates the ~21 sites that name a variant;
  `bucket_primitives()` takes a variant preference and its buckets carry
  the chosen `Buffer_mesh*` downstream, so record/draw fill sites follow
  the flowed choice instead of re-deriving a hidden default.
  `Id_renderer`, BLAS sources and glTF export always use `original`.

### The soup path (import time)

`optimize_triangle_soup()` (erhe_primitive `mesh_optimizer.hpp/.cpp`;
meshoptimizer.h stays private to the target) operates on a copy of the
source soup, on float data: analyze-before, `meshopt_generateVertexRemap`
(bitwise weld over the full interleaved vertex -- joints/weights included,
so skinned meshes are automatically safe), `meshopt_optimizeVertexCache`,
`meshopt_optimizeOverdraw` (threshold 1.05, float positions from a staging
copy), `meshopt_optimizeVertexFetch`, analyze-after. The result carries the
optimized soup plus the remap pair -- the triangle permutation and the
**forward source-to-output vertex remap** -- which drives both the
`Element_mappings` composition and the filesystem cache. The **soup path
passes empty (`{}`) source mappings to `compose_element_mappings()`** (its
remaps are not over the vertex buffer the source mappings index; contrast
the geometry path below).

One shared editor helper attaches the optimized soup after parse, per
**unique** render_shape (parse dedups shared `Primitive` slots), called from
all editor parse sites (import operation, `open_scene_gltf`, asset manager,
async load task, prefab library, controller visualization). `erhe_gltf`
itself stays ignorant of optimization; `src/example` stays unoptimized by
design.

The soup path covers the load-time window and soup-only consumers, and
feeds the cache; the durable win is the geometry path.

### The geometry path (the steady-state win)

In the editor, every imported soup mesh is replaced shortly after load by a
geometry-path rebuild (for edge lines) via the deferred finalize, and
procedural meshes only ever build here -- so this path is the steady-state
chokepoint. It emits one vertex per **corner**, i.e. it is unwelded by
construction, which is where the headline numbers come from.

- **Deferred allocation**: writers stage in CPU memory sized from mesh-info
  counts; the whole multi-buffer GPU allocation moves after the build,
  still as one transaction under `buffer_mesh_allocation_mutex()`. A writer
  never handed a destination range **drops** its staged bytes (destructor
  guard), so a failed allocation cannot flush to a default range and
  overwrite another mesh.
- **The meshopt passes run on the staged bytes, post-position-encode**: the
  staged (base) position is always float3; when the optimized format stores
  snorm16 the snapshot gather (`take_optimizable_snapshot()`) encodes during
  the copy -- against the build's AABB affine, the same one the decoders
  use -- so the weld runs on the final bytes (quantization merges more, and
  GPU data equals staged data exactly). The weld runs in **indexed** mode
  over the fill indices (a corner no triangle references is dropped --
  fine, the variant is fill-only), with **whole-stride compare streams**
  (`size == stride` per sink stream; safe because staging vectors are
  `resize()`d zero-filled). Then vertex-cache + overdraw with permutation
  recovery via a triple-to-triangle-id multimap consumed in emit order
  (deterministic; collisions only for bitwise-identical degenerate
  triangles; `triangle_to_mesh_facet` permuted in lockstep, bijection
  asserted), then `meshopt_optimizeVertexFetchRemap` (the multi-stream
  route). The **geometry path passes the build's own mappings** to
  `compose_element_mappings()` -- same function as the soup path, opposite
  rule.
- **No disk cache on this path** -- deliberate: the passes are fast
  relative to the geometry build itself and finalize is already deferred /
  async. Extend the cache here only if profiling disagrees.
- Both entry points -- synchronous `make_buffer_mesh(Build_info)` and the
  deferred prepare/commit import flow -- build every variant the
  `Build_info` requests; the deferred flow prepares all variants on the
  worker and commits them in the one atomic swap. Tool / preview / hotbar
  builders that discard the shape on return do not build the variant at
  all.

### The filesystem cache (soup path only)

`optimize_triangle_soup_cached()`: key is `erhe::hash::hash` (runtime
FNV-1a -- NOT `erhe_hash/xxhash.hpp`, which is a compile-time string hash)
over source index data + vertex data + vertex-format description + options
+ a format-version constant; entries live in `cache/mesh_optimizer/` under
the cwd (works on Quest via the Android chdir). An entry stores the
**remap pair**, not the optimized soup -- the pair fully determines it
(gather vertices through the derived output-to-source table, rebuild
indices as permutation-ordered triples through the forward remap), so a hit
runs no meshopt calls and the same pair feeds the mappings composition.
Writes are temp-file + rename (concurrent loader workers); any mismatch,
truncation or I/O error degrades to a miss with a warning -- **never a
failed load**. Content addressing dedups identical geometry across distinct
glTF meshes. Unbounded growth is accepted for v1.

### The edit bracket (invalidate at start, hold, re-optimize at end)

GPU vertex edits (paint colors, weight paint, live-drag positions) address
the per-corner original buffer through the mappings. Writing through the
welded variant is not possible -- merged corners share one slot -- and with
the base variant unquantized (requirement 9) any edited position is
representable in place: no AABB clamp, ever.

Requirement 11's bracket: when the edit STARTS (drag begin, stroke begin --
before the first GPU write) the tool calls
`Mesh::begin_optimized_variant_edit()`, which takes an **optimization
hold** on the `Primitive`, drops the live optimized shape frame-safely and
re-registers **every mesh sharing that `Primitive`** (draw-list records
bake the drawn variant's base_vertex and index ranges, and instances share
Primitives). Renderers then see the variant missing and fall back to
`original`; only one variant is live during an edit.

While the hold is active, `Primitive::publish_optimized_render_shape()` --
the single publication point every attach site goes through -- **refuses**,
so a build finishing mid-edit (a deferred finalize landing during a stroke)
can never put a pre-edit variant beside the in-progress edit. The holder
keeps the returned `Primitive` shared_ptr and releases via
`release_optimization_hold()` **on exactly that object** -- never through a
(mesh, index) lookup, because an operation executing mid-edit (undo during
a stroke) can swap the mesh's slot to a different Primitive. The
component-drag fork/extrude transfers its hold to the swapped-in primitive
mid-drag; the paint tool acquires holds lazily per touched primitive (a
hover-driven stroke can cross meshes).

At edit END the commit operations (`Move_mesh_vertices_operation`,
`Paint_weights_operation`, `Paint_colors_operation` -- paint is durable and
undoable: strokes write the geometry's `corner_color_0` attribute, the
attribute the builder prefers) rebuild the primitive **base-only
synchronously** (immediately renderable, requirement 9) and kick off the
background re-optimization: `kickoff_deferred_finalize()` dispatches the
same worker-prepare / `Scene_commit_queue`-commit finalize the import path
uses. The finalize decides re-optimization **at snapshot time under the
scene lock** (`optimized_render_shape` and the hold count are main-thread
state a worker must not read): a complete base mesh missing its requested
optimized variant, and not under an active hold, is rebuilt in full with
`prepare_geometry_buffer_mesh(..., force_rebuild = true)` -- the fresh
optimized variant comes out of the same staged bytes and the identical
base swaps in benignly, keeping the shape's committed normal style. The
accepted cost is the source build running twice per commit (once
synchronously for immediacy, once on the worker for the atomic swap).
Without worker contexts everything builds synchronously in the commit.

### Statistics

Per-primitive log line (counts, ACMR / overdraw / fetch before-after, weld
delta, elapsed) plus a process-wide `Mesh_optimize_totals`; the MCP
`get_memory_usage` tool's `mesh_optimization` section is the only complete
aggregate -- geometry-path optimizations land asynchronously in deferred
finalize tasks, so no single log point has them all (the
`finalize_imported_meshes` line is the soup-path picture only, and says
so). ACMR and overdraw are averaged **triangle-weighted** (per-triangle
costs; a per-primitive mean would weight a 4-triangle gizmo like a
92k-triangle board). Cache hits contribute vertex counts but no
ACMR/overdraw/fetch (they never ran `meshopt_analyze*`; the log says
"replayed from cache" instead of printing zeroes).
`fetch_bytes_before/_after` are **both in the optimized format's stride**,
isolating what the passes did; the 4 bytes/vertex the format change saves
is a separate, known figure.

### Position quantization (optimized variant only)

Quantization applies **only to the optimized variant** (requirements 9-10):
the content (base) formats always store float3, so `quantize_vertex_positions`
has no effect unless `optimize_meshes` is on, and the format choice has no
acceleration-structure gate -- every BLAS source is pinned to the original
variant, which is float3 by construction (`get_blas_position_input()` still
answers per Buffer_mesh and returns early on passthrough). The RT instance
records and the lightmap baker likewise derive their per-record encoding
from the original variant's storage format, so they are passthrough
automatically.

The affine AABB `(p - center) * inv_scale` encoding stays (meshoptimizer
has no affine helper; its exponent-based filters would change the shader
decode). The encode routes through `meshopt_quantizeSnorm(v, 16)` at the
two attribute-aware sites -- the `encode_position` branch of the soup
buffer build (the soup path re-runs the soup build against the optimized
sink format) and the geometry path's `take_optimizable_snapshot()` gather.
It is bit-identical to `float_to_snorm16` **only because both encode sites
pre-clamp to [-1, 1]** (the two functions differ in clamp order,
unreachable inside that interval).

The shader side needs no per-draw plumbing beyond what exists: the
position-encoding shader axis is derived from the draw's vertex format
(`Shader_key::derive()`), so a base draw decodes passthrough and an
optimized fill draw decodes snorm16 from the per-primitive
`position_scale` / `position_offset`. The shadow and forward prewarms warm
the optimized formats beside the content ones (one shadow variant per
{skinning state, position encoding}); the shadow classification's
derive-vs-bind keys agree for every mode because the optimized variant is
fill-only and the content + expanded formats are float together.

### Config plumbing

`Mesh_memory` holds a **reference** to the owner's `Mesh_memory_config`,
not a copy -- a copy would freeze the Settings toggle at its startup value.
`Mesh_optimize_options` flows through `Buffer_info` / `Build_info`,
populated in `make_primitive_buffer_info()`.

## Verification

Rendering identity is verified with A/B screenshot runs per the harness in
"How to verify"; figures are differing viewport pixels of 2 198 250, each
against a same-config control pair (the noise floor: at or near 0).

Expectations under the encoding split: `optimize_meshes` off-vs-on at
`quantize_vertex_positions=false` compares two float builds and differs
only by triangle order; at `quantize_vertex_positions=true` it compares a
float base against a snorm optimized build and is expected **non-zero**
(the quantization epsilon) -- that non-zero is simultaneously the proof
the optimized variant is selected and rendered.

- **Order-only identity** (ABeautifulGame, quantize=false): off-vs-on at
  the control-pair noise floor (2 px). On Bistro the order residue is
  **0.163%** -- one-LSB differences where triangle order meets
  order-dependent shading in alpha-blended foliage; with the two reorder
  passes off (weld + fetch on) the run is pixel-exact, so it is not a weld
  defect. See Traps.
- **Quantization epsilon** (ABeautifulGame, quantize=true): off-vs-on
  **0.84%**, 96% of differing channel samples at 1-4 LSB, the rest
  silhouette-edge pixels. The base render is pixel-identical across the
  quantize flag (requirement 8 observable at pixel level).
- **Mutation check**: halving the optimized index buffer moves 34.0% of
  viewport pixels -- the code path under test is demonstrably reached.
- **Byte check** (`get_mesh_buffer_info` / `get_mesh_buffer_data`, which
  read the actual GPU bytes and name which build they read): base =
  `format_32_vec3_float` stride 12, `passthrough`; optimized =
  `format_16_vec3_snorm` stride 8 (4-aligned), `snorm16x3_aabb`,
  fill-only, no facet-id attribute; decoded int16 positions land inside
  the primitive AABB. The tools also make the variant invariants
  observable (fill-only index ranges, the facet-id-free stride).
- **Cache**: baseline vs fully-cached, cold vs warm, and
  deliberately-corrupted-entry runs all compare at the noise floor;
  corrupted entries re-optimize. Content addressing dedups identical
  geometry (15 primitives -> 8 entries on ABeautifulGame). Hit cost ~0 ms;
  uncached optimize cost is ~30-190 ms per imported primitive, which is
  what justifies the cache.
- **Measured characteristics** (ABeautifulGame session aggregate): the
  corner-per-vertex geometry build welds ~-61% session-wide
  (2 138 692 -> 833 132 vertices; -81.7% on the chess pieces), ACMR
  1.96 -> 0.87, overdraw 1.21 -> 1.04, vertex-fetch bytes **-62%**.
  Overdraw stays on: the weld's fetch win dwarfs what the reordering
  trades away. The soup path welds +0.0% on already-welded glTFs and
  procedural platonic solids weld +0.0% (distinct flat corner normals) --
  both correct.
- **Interaction sanity** (headless, run on and off): 52 `pick_at` probes
  hit the same node / mesh / **facet id** in both runs (hit positions may
  differ ~1e-6 m -- the reordered BVH's rounding); a convex-hull drop
  onto a mesh collision shape rests at a **bit-identical** position; glTF
  export/import round-trips to exactly the source
  vertex/edge/facet/corner counts.
- **Interactive session** (real mouse, `optimize_meshes` on): paint with
  undo/redo, weight paint, shared-`Primitive` edits (two instances, one
  edited, both stay correct), in-AABB and out-of-AABB vertex drags (no
  clamp, no pop) all behave correctly against a live draw list, and the
  optimized variant returns after each edit ends.
- **Unit tests**: the multi-stream optimizer core, the geometry-path
  encoded-and-welded optimized build (box round-trip through CPU sinks),
  live-edit variant invalidation, the edit bracket and the publish gate.
  Note: `ctest` at the build_tests root aborts on the
  erhe_graphics_gpu_tests discovery include when that target has not been
  built (it is not in the default target) -- build it explicitly or run
  ctest per test directory.
- **Build sweep** for a change here: ninja vulkan Debug, VS opengl Debug and
  the Quest APK. The OpenGL build matters on its own, because this work
  touches vertex formats and GLSL and the Vulkan build cannot speak for that
  backend.

### How to verify: the A/B screenshot harness

`scripts/mesh_ab_capture.py <off|on|cache> <out.png>`; its docstring
carries the full rationale. Every one of these matters:

- MCP port is 3743, NOT 8080.
- `set_ddgi {enabled:false}` -- the single biggest source of run-to-run
  divergence -- and `advance_time {mode:"paused"}`.
- The harness pins the window layout itself
  (`cache/desktop_windows.pinned.json`, snapshotted on first run). The
  editor rewrites `config/editor/desktop_windows.json` on exit, and a
  different layout moves the viewport -- a ~96% pixel difference that is
  not a rendering difference. `desktop_windows.json`,
  `editor_settings.json` and `mesh_memory.json` are session state: restore
  with git checkout after runs.
- Crop the ImGui panels out (viewport starts near x=350, y=75 at
  2304x1200).
- ALWAYS run a same-setting control pair first -- and a passing control
  pair is NECESSARY, NOT SUFFICIENT: a saturated or black viewport
  compares equal to another one (check the saturated-pixel fraction), and
  the code path under test may not be reached at all -- MUTATION-CHECK it
  (halving the optimized index buffer moved 34.0% of viewport pixels).
- Kill leftover editors: the harness refuses to start when something
  already answers on 3743 -- a leftover keeps the port, the new editor
  fails to bind, and every RPC then drives the OLD process with the OLD
  config.

Bistro, specifically:

    ERHE_SHOT_SCENE=res/editor/assets/niagara_bistro/bistro.gltf
    ERHE_SHOT_EXPOSURE=0.001
    ERHE_SHOT_FRAME=0
    ERHE_SHOT_EXE=build_ninja_win_vulkan_release/bin/editor.exe

Bistro saturates the viewport to near-white at default exposure (still
almost entirely clipped at 0.02, measured); 0.001 makes it readable.
`ERHE_SHOT_FRAME=0` keeps the asset's authored camera: `frame_scene()`
costs one RPC per node -- thousands of round trips on Bistro, minutes per
run, looks like a hang -- and the authored camera is just as deterministic.
Both sides of one comparison must use the same executable.

## Traps

Each of these cost a review or debugging round. Do not rediscover them.

- **Attach the optimized variant BEFORE `mesh->update_rt_primitives()`
  registers the mesh with the draw list.** Registration bakes `base_vertex`
  and the index ranges of whichever variant is live at that moment.
- **An empty index range reads as "nothing to draw", not "fall back".**
  Variant resolution must be `Primitive_mode`-aware, or an edge-line pass
  drops the primitive instead of using the original.
- **The facet id must be OUT of the weld compare** -- leaving it in merges
  nothing. The mechanism is the separate optimized vertex format; both
  paths must build in it, and a format missing from any consumer of
  `get_all_vertex_formats()` (repack, lockstep sizing, vertex-input
  registration) fails silently and differently.
- **`compose_element_mappings()`: the soup path passes `{}` for source
  mappings, the geometry path passes the build's own.** Same function,
  opposite rule.
- **`Mesh_memory` must hold a REFERENCE to the owner's config** -- a copy
  freezes the Settings toggle at its startup value.
- **Bracket the edit at its start, and re-register every mesh sharing the
  Primitive** -- not just the edited one. Release the optimization hold on
  the exact Primitive object begin handed back, never through a
  (mesh, index) lookup: an operation executing mid-edit (undo during a
  stroke) can swap the slot to a different Primitive, and the mid-drag
  fork/extrude swap must transfer the hold.
- **`async_for_nodes_with_mesh()` takes `item_host_mutex` ITSELF** -- never
  call it (or `kickoff_deferred_finalize()`) while holding that mutex; a
  non-recursive `std::mutex` relocked by its owner throws "resource
  deadlock would occur". The edit commit operations hold the lock for
  their whole apply(), so they explicitly unlock before the kickoff.
- **Welding is bitwise over every byte including padding** -- safe only
  because staging is `resize()`d (zero-filled) before attribute writes.
  Keep that property or the weld silently degrades.
- **The two snorm16 quantizers agree only inside [-1, 1]** -- their clamp
  orders differ; the encode sites' pre-clamp is what makes them
  bit-identical.
- **A cache hit has no analyze statistics** -- do not read its zeroes as
  "optimization achieved nothing"; `Mesh_optimize_statistics::measured`
  and the log wording exist for this.
- **Read the optimization aggregate from `get_memory_usage`, not the
  log** -- geometry-path optimizations land asynchronously; no log point
  has them all.
- **Bistro's off-vs-on order residue (0.163%, one-LSB alpha-foliage
  pixels) is the triangle reordering, not a weld defect** -- do not chase
  it again; the weld+fetch-only run is pixel-exact. With quantization on,
  off-vs-on additionally carries the expected float-base-vs-snorm-optimized
  epsilon (see Verification).
- **Enabling optimization reorders triangles, so the CPU BVH disk cache
  (keyed on triangle positions) takes a one-time full miss/rebuild sweep
  on first optimized load.** Expected; noted in logs.
- **`get_mesh_attribute_values` reads SOURCE geometry, not GPU bytes** --
  buffer-level checks need `get_mesh_buffer_info` / `get_mesh_buffer_data`.
- **OOM only, known**: a failed mid-transaction multi-pool allocation in
  the optimized-build path abandons the partial transaction (successful
  ranges released via `~Buffer_mesh` into the retired list) -- the same
  behavior as the pre-existing allocation sites.

## Future work

- [Mesh optimization](../plans/meshoptimizer.md) - runtime perf measurement,
  Quest verification, encoding candidates, LOD and meshlet seams.
