# Draw list renderer — requirements

Status: DRAFT — all open questions resolved (Q1–Q11); revised after
independent review (2026-08-15); awaiting approval for planning.

## 1. Problem statement

`erhe::scene_renderer::Forward_renderer` (color passes) and
`Shadow_renderer::draw_shadow_casters` (shadow maps) recompute everything from
scratch on every call. With thousands of primitives in the scene this does not
scale, because these are called many times per frame (shadow map per light /
cube face, opaque content, translucent content, selected/not-selected passes,
edge line composition passes, ID render), and each call repeats work whose
inputs did not change since the previous frame:

- `bucket_primitives()` walks every mesh and every primitive, derives a
  `Shader_key` per primitive, and linearly scans the bucket vector
  (`Render_bucket::accept()`) to place each primitive. This is O(meshes ×
  primitives × buckets) per `render()` call, and it runs once per entry of
  `base_render_pipelines` inside each call.
- Bucket contents are identical frame to frame whenever the scene composition
  did not change, yet the buckets are rebuilt and thrown away every call.

`Scene_root` already owns persistent per-domain systems for physics
(`m_physics_world`) and ray tracing (`m_raytrace_scene`). Rendering is the
missing third domain: we want a persistent, incrementally maintained
rendering-side representation of the scene, so per-frame work is proportional
to what changed and what is drawn — not to the total primitive count times the
number of passes.

## 2. Goals

- G1: Registering an object once produces persistent draw lists; subsequent
  frames reuse them without re-bucketing.
- G2: Per-frame CPU cost of issuing a pass is proportional to the number of
  draw lists and entries drawn in that pass, with cache-friendly iteration.
  "No per-entry pointer chasing" is the goal for static lists; dynamic and
  skinned lists necessarily read per-frame scene state (node world transform,
  joint data) while building their primitive upload.
- G3: Output parity: for supported passes the rendered image is identical to
  what `Forward_renderer::render()` / `Shadow_renderer` produce today, and the
  pipeline/state selection per draw list is the same as per bucket today. The
  draw call *count* may differ where draw list identity is deliberately finer
  (layer id, R13) or coarser (shadow material coarsening, R4) than today's
  bucket identity.
- G4: The design leaves room for future optimizations on static objects
  (e.g. cached per-primitive GPU data, culling structures) without API changes.

## 3. Scope

### Initial scope (included)

- Shadow map rendering (triangle fill; all three caster variants used today:
  depth-only, depth-only + distance technique, point-light cube; material
  ignored except opacity/blending classification).
- Main color rendering (triangle fill, opaque and translucent).

### Excluded (future work)

- Shader debug override visualizations.
- Edge line passes (all methods) and other non-`polygon_fill` primitive modes.
- ID render / picking passes.
- Incremental (entry-granular) mirroring of mesh/primitive/material edits.
  Edits ARE handled in the initial scope, but coarsely — by re-registering the
  object or rebuilding (R12); fine-grained updates are future work.
- Culling (Q6): the initial version draws every registered entry that passes
  the flag filter, exactly as `Forward_renderer` does today. Frustum culling
  on the entry AABB is the FIRST item on the future-work list once the
  initial renderer works — `Draw_list_entry` carries the AABB from day one
  (R15) so culling slots in without a data-model change.
- GPU-driven culling or draw generation (design must not preclude it).

### Fallback

`Forward_renderer` (color) and `Shadow_renderer::draw_shadow_casters` (shadow)
stay intact and are used for every pass that `Draw_list_scene` does not
support. Both fallback paths and `Draw_list_scene` must be able to run in the
same frame against the same scene.

## 4. Existing structure to build on

Stub files exist and are the intended home of the implementation:
`src/erhe/scene_renderer/erhe_scene_renderer/draw_list_scene.*`,
`draw_list.*`, `draw_list_entry.*`, `draw_list_object.*`
(currently empty classes; `draw_list.cpp` etc. are stubs).

The partitioning today is defined by `Render_bucket` /`bucket_primitives()`
(`mesh_memory.cpp`). A primitive's bucket identity is:

- `primitive_mode`
- vertex input key (`bucket_vertex_input_key`)
- index buffer identity and vertex buffer identities (pool id + buffer id)
- `Shader_key` hash (derived from material, vertex format, skinned flag, plus
  the pass "environment" key)
- `negative_determinant` flag (mirrored transforms → front-face-flipped
  pipeline variant)
- blending mode (opaque vs translucent, via `Blending_mode_policy`). Note the
  null-material case: `derive(nullptr)` yields no blending mode, which today
  is excluded from opaque passes but included in translucent passes
  (`mesh_memory.cpp` policy switch) — R1 must reproduce this.

Mesh layer id is NOT part of bucket identity today (content + controller
meshes share buckets); R13 adds it deliberately (Q3).

Draw list identity must mirror this partitioning (G3), with one important
split, described next.

Note the two purposes use different environment keys today: the color path
builds a full environment key (light counts, shadow settings, multiview,
debug); the shadow path uses an *empty* environment key plus force masks. Draw
list resolution must reproduce this per purpose so shadow resolution hits the
same variants `Shadow_renderer::prewarm_pipelines` warms (R22).

### Environment component inventory — what can be baked

Today's `Shader_key` and `Render_parameters` mix primitive-derived inputs
(material features, vertex format, skinned flag, blending mode — trivially
bakeable per draw list) with "environment" inputs. The table below inventories
every environment input, with how the editor actually drives it for the
in-scope passes (shadow map + content fill opaque/translucent, from
`app_rendering.cpp` / `composition_pass.cpp` / `shadow_renderer.cpp`), and the
resulting bake classification:

| Component | Source in editor today | Changes when | Bake classification |
|---|---|---|---|
| Light counts (6 axes: directional/spot/point × shadowed/not) | `compute_light_layer_partition(lights)` per color `render()` call; depends on each light's type, `cast_shadow`, and `is_active()` (point range > 0) | Light add/remove goes through `Scene_root::register_light`; type / range / cast_shadow are edited by direct member writes with NO event (properties window, MCP) | **Scene config** — bake; because there is no reliable edit event, recompute the partition (O(lights), trivial) at the start of each color `draw()` and re-resolve only if it differs from the cached one |
| `SHADOW_FILTER`, `SHADOW_BIAS`, `SHADOW_TECHNIQUE`, `SHADOW_DEPTH_BITS` (color path) | Graphics preset (`app_settings`), identical for every pass | User changes graphics preset (`graphics_settings` message bus event) | **Global config** — bake; recompute on preset change. (The shadow path's distance-technique toggle is the same preset input but is modelled as the R4a sub-variant, not as config) |
| `SHADER_MULTIVIEW_COUNT` | key value 0 when `views.size()==1` (desktop viewports), N when `views.size()==N≥2` (XR) | Per scene view; desktop mirror + headset can render the same scene in one frame | **View-config axis** — enumerated up front from the same source prewarm uses (`multiview_view_counts`); a draw list caches a resolved variant per enumerated view config; a not-yet-enumerated config seen at draw time resolves lazily once (R19) |
| `SHADER_DEBUG` | 0 for in-scope passes (bone N·V pass has an override — out of scope) | Debug UI | **Fixed to 0** — non-zero falls back to `Forward_renderer` |
| Force-enable mask | 0 for content fill; shadow: `VARIANT_DEPTH_ONLY` (directional/spot), `+ VARIANT_SHADOW_DISTANCE` when the distance technique is active, `VARIANT_SHADOW_CUBE` for point-light cube faces (on a cull_none base pipeline); `EDGE_LINES_FROM_ID` toggled onto the fill passes when the ID-buffer edge method is active (fallback while active — Q9); other masks (`SOLID_WIREFRAME`, `EDGE_LINES_CORNER_CAP`, `VARIANT_BRUSH_PREVIEW`) belong to out-of-scope passes | Light type per shadow pass; graphics preset; editor settings toggles | **Baked as a shadow sub-variant axis** {depth-only, depth-only+distance, cube} — resolved per shadow list, selected by the caller per shadow pass; `EDGE_LINES_FROM_ID` → fallback (Q9) |
| Force-disable mask | Always 0 everywhere in the editor today | — | **Fixed to 0** |
| `Blending_mode_policy` | `opaque_primitives_only` / `translucent_primitives_only` for fill passes; opaque-only for shadow | Static per pass | **Already baked** — this is the draw list's blending classification (R7) |
| `Item_filter` | Selected / not-selected / visible splits | Selection and visibility change constantly | Not a shader input — see Q1/Q2 |
| `base_render_pipelines` | Static pass definitions (depth/stencil/raster state) | Never at runtime | Supplied by the caller at draw time, like today; pipeline resolution cached per (draw list × pass) |
| `shader_stages_override`, `color_blend_override` | Only out-of-scope passes (selection stencil mask, macOS GL edge lines) | — | **Not supported** — such passes use the fallback |
| `Primitive_interface_settings`, exposure, camera/viewport, light *data*, joint matrices, materials contents | Per-pass / per-frame UBO and primitive-buffer contents | Every frame | Not variant-affecting — per-frame upload, orthogonal to draw list identity |

### Environment components affect resolution, not partitioning

The environment key is the shared base that per-primitive `Shader_key::derive`
builds on, so a uniform environment shifts every primitive's key identically:
it can change which variant/pipeline a draw list resolves to, but never which
entries a list contains. Consequence: an environment configuration change
(R18) re-resolves cached pipelines only — it never re-buckets and never
touches entries. This invariance is not just an observation about today's
code; it is required going forward (R21).

The only components that interact per-primitive, and therefore *would* affect
partitioning, are:

- `SHADER_DEBUG` × `shader_debug_filter` (per-mesh axis drop) — this is why
  shader debug must stay out of scope / fixed to 0 rather than becoming a
  config axis.
- `Blending_mode_policy` × material blending mode — membership, already baked
  as the list's blending classification.
- Force masks in general can coarsen partitioning (merging lists that differ
  only in a forced bit). The in-scope bits (`VARIANT_DEPTH_ONLY`,
  `VARIANT_SHADOW_DISTANCE`, `VARIANT_SHADOW_CUBE`; the ID-buffer edge method
  uses the fallback, Q9) are set exclusively by force masks —
  `Shader_key::derive` never sets them from a material — so they are
  guaranteed uniform, pipeline-only effects. Any future variant that forces a
  bit materials can also set must be re-analyzed.

Unrelated to environment, but the one identity component that could change at
runtime: `negative_determinant` is transform-driven. In the initial scope a
runtime sign flip on a registered object asserts (R10b); re-listing on flip is
future work.

Requirements following from this:

- R17: Each draw list caches its fully resolved shader stages
  (`Shader_variant_cache` result) per resolution config, resolved at
  registration time and on configuration change (plus the one-off lazy paths
  of R19 / R4a) — not per frame, not per draw, and never per entry. The *render pipeline* additionally depends on
  inputs known only at draw time (`Base_render_pipeline`, render-pass
  descriptor, color blend); `Base_render_pipeline::get_pipeline_for` is
  already a hash-cached lookup keyed on those, so per-list pipeline lookup at
  draw time is one hash probe, not a compile. Cache key for the resolved
  stages: (list identity × purpose sub-variant × view config × environment
  configuration).
- R18: `Draw_list_scene` holds one **environment configuration** for the
  color purpose: light-count partition + shadow settings from the graphics
  preset. (The shadow purpose has no environment configuration: its only
  varying input, distance technique on/off, is expressed as the caller-
  selected sub-variant of R4a.) Changing it invalidates and re-resolves the
  cached stages of the affected lists — never their contents (R21).
  Triggers: graphics preset via the `graphics_settings` message-bus event;
  light-count partition by cheap recompute-and-compare at the start of each
  color `draw()`, because light type / range / cast_shadow are edited by
  direct member writes with no event (see inventory table).
- R19: Multiview is a small enumerable axis of the cached resolution
  ({single-view, multiview-N}), not a rebuild trigger: a desktop viewport and
  an XR view of the same scene render in the same frame. The set of view
  configs is enumerated up front from the same source prewarm uses
  (the `multiview_view_counts` list, today a local in `prewarm.cpp` — must be
  exposed as a shared source) and resolved at registration; a config not in
  that set encountered at draw time resolves lazily once and is added to the
  set. This lazy path and R4a's first-use shadow sub-variant resolution are
  the only two exceptions to R20; both are by construction one-offs, not
  steady state.
- R20: Steady state (no configuration change, no new view config, no
  first-use shadow sub-variant): `draw()`
  performs zero `Shader_key` derivation and zero `Shader_variant_cache`
  lookups; it uses the cached stages directly and one hash probe per list for
  the pipeline (R17).
- R21 (environment invariance): Draw list contents — the set of draw lists,
  their identities, and their entries — MUST be invariant under every
  environment change (light-set changes, graphics preset changes, view config,
  editor setting toggles such as the ID-buffer edge method). Environment
  changes may only invalidate and re-resolve cached variants/pipelines (R17),
  never add, remove, move, or re-partition entries. Entries change only on
  scene-side events: registration/unregistration (R1/R2), re-registration on
  mesh/material edits (R12), and the rebuild hook (R1a); flag changes update
  entry state in place (R12a); the negative-determinant flip asserts in the
  initial scope (R10b).
  Corollary: any future variant axis or forced shader bit that would break
  this invariance (per-primitive interaction, as analyzed in §4) must be
  redesigned or kept on the `Forward_renderer` fallback — it must not be
  bolted onto `Draw_list_scene`.
- R22 (variant compilation, resolves Q10): `Forward_renderer` and
  `Draw_list_scene` share the same `Shader_variant_cache` and pipeline caches
  — the same variant is never compiled twice because two renderers want it.
  The existing prewarm paths (`prewarm_standard_variants`,
  `Shadow_renderer::prewarm_pipelines`) stay authoritative: they must keep covering the
  fallback passes regardless, and with a shared cache the draw-list
  resolution (R17) is then a cache hit for prewarmed content — PROVIDED the
  keys match. Consequently, wherever draw list identity intentionally differs
  from bucket identity, the corresponding prewarm must be updated to warm the
  draw-list key too (concretely: shadow prewarm must warm the coarsened
  shadow key of R4). On a cache miss (unwarmed variant appears via
  registration or a config change), compilation happens at registration /
  invalidation time — off the per-frame hot path. `draw()` itself never
  triggers compilation (except the one-off lazy paths of R19 and R4a); a
  list whose variant is not yet resolved is skipped with a warning, exactly
  like today's "No shader variant for bucket" behavior. Minimum-stall
  coexistence of both renderers is the acceptance criterion.

## 5. Components and requirements

Ownership chain: `Scene_root` owns one `Draw_list_scene` (unique_ptr, same
pattern as `m_physics_world`). `Draw_list_scene` owns its `Draw_list_object`s
and `Draw_list`s. A `Draw_list` owns its `Draw_list_entry`s.

### 5.1 Draw_list_scene

Registration:

- R0 (registration pattern, resolves Q4): registration follows the existing
  physics/raytrace pattern in `Scene_root`: `Scene_root::register_mesh` /
  `unregister_mesh` (driven by the item-host attach/detach path, as with
  `attach_rt_to_scene` / `Node_physics`) also register/unregister the mesh
  with `Draw_list_scene`. No separate editor-side call sites.
- R0a (library layering): `Mesh` lives in `erhe::scene`, which does not and
  must not link `erhe::scene_renderer`. Every scene→draw-list notification
  named in this document (registration, primitive-set change, material
  reassignment, flag mirroring, transform update) therefore goes through the
  `erhe::scene::Scene_host` interface (`scene_host.hpp`) — new virtuals
  alongside `register_mesh` / `unregister_mesh` — implemented by `Scene_root`,
  which owns the `Draw_list_scene`. `Mesh` calls its host; it never sees
  `Draw_list_scene`. (This is exactly how `attach_rt_to_scene` and
  `Node_physics` are reached today.)
- R1: `register` function taking `Draw_list_object_create_info`. For each
  primitive of the object it determines the draw list(s) the primitive belongs
  to (creating draw lists on demand), and appends a `Draw_list_entry` per
  (primitive × draw list). The classification must mirror
  `bucket_primitives()` (same skips: null primitive, null renderable mesh,
  zero index count; same null-material blending classification — no blend
  mode → excluded from opaque, included in translucent).
- R1a (rebuildability, resolves Q7): `Draw_list_scene` retains, for every
  registered object, a record sufficient to re-run its classification from
  scratch (the `Draw_list_object` keeps the create-info-level inputs, not just
  the resulting entries). An explicit invalidation hook drops and rebuilds all
  draw lists from these records. Clients in the initial scope: material
  *content* edits that change list identity (R12), and any future event that
  invalidates baked state wholesale. (Note: `Mesh_memory` / `Buffer_pool`
  never *move* existing allocations at runtime — pools only append blocks —
  so pool-level residency churn is NOT a client of this hook. However a live
  `Primitive`'s `Buffer_mesh` CAN be replaced in place:
  `Primitive_render_shape::commit_geometry_buffer_mesh()` and
  `make_buffer_mesh()` move-assign a new `Buffer_mesh` over
  `m_renderable_mesh` (freeing the old ranges), and this runs on attached,
  registered meshes in ordinary use (deferred glTF / prefab finalize via
  `async_raytrace_kickoff_operation`). That path calls
  `Mesh::update_rt_primitives()`, which is why R12 hangs the re-register
  hook there — the per-object re-register (R12), not this wholesale hook, is
  what keeps baked buffer ranges valid.)
- R1b (dependencies): classification needs `Mesh_memory`
  (`get_vertex_input` for the vertex format that feeds `Shader_key::derive`)
  and resolution needs `Shader_variant_cache`; `Scene_root` currently has
  access to neither. They are injected when `Draw_list_scene` is created. A
  `Scene_root` constructed without `Mesh_memory` / `Shader_variant_cache`
  injected (scene roots that are never rendered) has no `Draw_list_scene`
  (`m_draw_list_scene == nullptr`); registration and flag mirroring are
  no-ops in that case, mirroring how `enable_physics == false` leaves
  `m_physics_world` null. (The null graphics backend is still a `Device`
  with a `Mesh_memory`; it gets a `Draw_list_scene` like any other backend
  and must stay in sync.)
- R2: `unregister` function that removes all entries belonging to an object.
  Removal must not invalidate the draw lists' suitability for hot-path
  iteration (compaction strategy is an implementation choice, but stale
  entries must not be drawn).
- R3: A primitive is added to every draw list variant it participates in
  (e.g. one entry in a color list and one in a shadow list).

Variants — each registered primitive is classified along two axes:

- Purpose: main color | shadow map.
- Mobility: static (transform never changes; enables future optimizations) |
  dynamic (transform can change) | skinned.

giving the six initial variants:

| Variant | Purpose | Mobility |
|---|---|---|
| color / static | main color | static |
| color / dynamic | main color | dynamic |
| color / skinned | main color | skinned |
| shadow / static | shadow map | static |
| shadow / dynamic | shadow map | dynamic |
| shadow / skinned | shadow map | skinned |

- R4: Shadow variants ignore material except for what affects shadow casting,
  so primitives that differ only in other material features share shadow draw
  lists. This is a deliberate coarsening relative to today's shadow buckets
  (which derive the key from the full material even though the position-only
  vertex path ignores those bits). Concretely, the shadow list *shader key*
  is exactly {`USE_SKINNING`, the forced VARIANT bits of the sub-variant} —
  no material-derived bits at all, including the material-gated
  `USE_VERTEX_VARYING_*` bits, which `ERHE_VARIANT_POSITION_PASS` ignores.
  Note `Shader_variant_cache` is keyed on `Shader_key` ONLY — the vertex
  format is used just at first compile (attribute locations are sequential
  per format), so the coarsened key yields one variant per {USE_SKINNING}
  regardless of format; this is correct only because position is location 0
  in every mesh format and joints/weights sit at the same locations in the
  skinned formats — assert that in the shadow prewarm. Shadow list *identity*
  additionally keeps `negative_determinant`,
  buffer set, layer id and blending classification (opaque-only membership,
  as the shadow pass uses `opaque_primitives_only` today).
  `Shadow_renderer::prewarm_pipelines` must be updated to build this
  identical coarsened key (R22).
- R4a: Shadow lists carry a resolved-stages sub-variant axis {depth-only,
  depth-only + distance, cube} (see inventory table); `draw()` for the shadow
  purpose selects the sub-variant per pass, and the caller supplies the
  matching base pipeline and color-blend state exactly as
  `Shadow_renderer::render` does today. Today's shadow prewarm warms only
  depth-only; distance and cube compile on first use. Initial scope keeps
  that: a sub-variant's stages are resolved on the first `draw()` that
  selects it (one-off, same carve-out as R19's lazy view config), so unused
  sub-variants are never compiled. Extending prewarm to all three is a
  follow-up.
- R5: The variant set must be extensible (edge lines, ID render, debug
  visualizations later) without reworking registration. Passes not covered by
  a variant fall back to `Forward_renderer` / `Shadow_renderer`.

Drawing:

- R6: `draw` function that renders exactly one purpose variant selected by the
  caller, into a caller-provided render encoder/pass (all mobility classes of
  that purpose in one call — the split by mobility exists for update cost and
  future optimization, not for pass structure).
- R6a (layers, resolves Q3): `Draw_list_scene` covers all mesh layers.
  `draw()` additionally takes the set of mesh layer ids to include (today's
  passes draw layer subsets, e.g. content + controller for content fill,
  rendertarget alone for the rendertarget pass). Because layer id is part of
  draw list identity (R13), layer selection selects whole lists — it is never
  a per-entry test.
- R7 (blending class selection): `draw()` takes a blending-class selector —
  opaque only, translucent only, or both — because the editor's opaque and
  translucent content fills are separate composition passes with other passes
  (sky, grid, edge lines) in between, and the rendertarget pass uses
  `allow_all`. When both are selected in one call, all opaque lists are drawn
  before all translucent lists. Translucent draw order within the class is
  implementation-defined (see C1).
- R7a (draw-time flag filtering, resolves Q1/Q2): `draw()` accepts a filter
  with `Item_filter` semantics (require-set / require-clear bit masks)
  evaluated against each entry's mirrored flag bits (R12a) while the indirect
  draw commands are built. Entries rejected by the filter emit no draw
  command but remain in their lists. This is how today's selected /
  not-selected / visible pass splits are expressed: same lists, different
  filter per pass.
- R8: Per draw list, drawing performs: fetch the cached pipeline (R17/R20 —
  resolved earlier via `Shader_variant_cache` and
  `Base_render_pipeline::get_pipeline_for`), bind index/vertex buffers, fill +
  bind per-primitive and draw-indirect ranges, issue one
  `multi_draw_indexed_primitives_indirect`. This mirrors the per-bucket body
  of `Forward_renderer::render()` minus the per-call key derivation and cache
  lookups.
- R8a (GPU buffer ownership and bind contract): `Draw_list_scene` owns NO GPU
  buffers or texture heap. The per-pass Camera / Light / Material / Joint /
  texture-heap update + bind sequence remains the responsibility of the
  owning renderer (`Forward_renderer` for color, `Shadow_renderer` for
  shadow), and `draw()` is invoked *inside* that sequence with the renderer's
  `Primitive_buffer` and `Draw_indirect_buffer` supplied for filling the
  per-primitive and indirect ranges. `Primitive_buffer::update` and
  `Draw_indirect_buffer::update` gain entry-based overloads alongside the
  existing `Render_bucket` ones. Consequence for R15: material and joint GPU
  slots (`material_buffer_index`, `joint_buffer_index`) are assigned per
  `Material_buffer::update` / `Joint_buffer::update` call and therefore
  cannot be baked into entries — an entry stores a stable reference to the
  material / skin (index into the object's primitive array or an equivalent
  handle) and the slot is read at upload time, as today.
- R9: Frame-varying per-entry GPU data (primitive transforms, joint matrices)
  is still uploaded per frame for dynamic/skinned lists. Static lists must be
  structured so a future change can skip or cache this upload (G4); doing the
  caching now is out of scope.

### 5.2 Draw_list_object

- R10: Represents one registered scene object (mesh) inside `Draw_list_scene`
  and keeps the mesh and its primitives alive (owning references) for as long
  as it is registered.
- R10a (mobility classification, resolves Q5): static / dynamic mobility is an
  explicit flag in `Draw_list_object_create_info`, supplied by the caller at
  registration; skinned means `mesh->skin != nullptr` (note the shader key's
  `USE_SKINNING` additionally requires joint vertex attributes, so a skinned
  list may still resolve a non-skinning variant — that is a key matter, not a
  mobility matter). Because registration happens inside
  `Scene_root::register_mesh` (R0), which has no mobility information and
  there is no static item flag today, the initial scope registers every
  non-skinned mesh as **dynamic**. The API carries the flag from day one so a
  static classification source (item flag, asset metadata) can be added
  without API change; static-list optimizations and that source are future
  work. Initial scope: a transform change on an object registered as static
  is a bug — assert (debug), no auto-reclassification.
- R10b (determinant stability, resolves Q11): `negative_determinant` is baked
  into list identity. Registration MUST sample it from the node's current
  world transform (`world_from_node()` determinant), NOT from the mesh's
  `Item_flags::negative_determinant` bit: `Node_attachment::set_node` calls
  `handle_item_host_update` (→ `register_mesh`) *before*
  `handle_node_transform_update` (which sets the flag), so a mesh attached
  under a mirrored parent would otherwise register with the flag clear and
  flip one call later. Initial scope: a runtime determinant sign change on a
  registered object is a bug — assert (debug), same policy as R10a. Known
  cases that would trip it in ordinary editor use: mirroring an object by
  negative scale interactively; reparenting a node under a mirrored parent
  within the same host (transform update only, no re-registration); and a
  cross-host reparent under a mirrored ancestor (`Node::handle_parent_update`
  registers before `update_world_from_node()`, and descendants' world
  transforms refresh only in the queued propagation pass, so registration
  can still sample a stale determinant there).
  Re-listing on flip is the same mechanism as R12 (re-register from the
  transform-update path via `Scene_host`, R0a) and is the recommended
  follow-up; the assert stands per Q11 unless revisited.
- R11: Knows which entries in which draw lists belong to it, so unregistration
  and future incremental updates are O(entries of this object), not a scan of
  all draw lists.
- R12a (flag mirroring, resolves Q1): dynamic per-item state is mirrored into
  the draw lists through a `Draw_list_object` API: the caller sets/clears
  flags on the object, and the implementation locates that object's entries
  (via R11) and updates a flag-bits field stored in each entry, in place.
  Cost is O(entries of that object). No entries are added, removed, or moved
  between lists by a flag change (flags are entry *state*, not list
  *identity*, R21). The existing item hook `Mesh::handle_flag_bits_update`
  (already used to mirror visibility into the raytrace instance) is the
  mirroring path. The mirrored value is the full `Item_flags` word, because
  today's filters test bits across it (visible, content, controller,
  selected, hovered_in_item_tree, shadow_cast, proxy_hidden, ...); the entry
  field must be wide enough (64 bits) for `Item_filter` to apply unchanged.
  Implementation gotcha: `Mesh::handle_flag_bits_update` currently
  early-returns unless the `visible` bit changed; the mirroring branch must
  run for any change to a mirrored bit (selected / hovered / ...).
- R12 (mesh / material edits — coarse handling in initial scope): registered
  meshes are NOT immutable in practice: the editor calls
  `Mesh::set_primitives` / `add_primitive` on attached meshes (mesh
  operations, undo/redo, geometry-graph re-bake, merge, vertex move),
  reassigns materials in place (properties window, material paint tool), and
  edits material contents that change list identity (bind texture, BXDF
  model, blending mode); and a live primitive's `Buffer_mesh` (index/vertex
  ranges, buffer ids) can be replaced in place without any primitive-list
  change (R1a note). Requirement: the object is re-registered with
  `Draw_list_scene` (unregister + register, O(entries of the object)) from
  a new `Scene_host` virtual invoked by **`Mesh::update_rt_primitives()`**
  (reached from `set_primitives`, `add_primitive`, and the deferred
  buffer-mesh commit path) AND from **`Mesh::clear_primitives()`** (which
  does NOT call `update_rt_primitives` today and is used on attached meshes
  by `geometry_graph_mesh.cpp`). Note the raytrace analogy is imprecise:
  for an attached mesh the editor brackets `update_rt_primitives` with
  `Scene_root::begin_mesh_rt_update` / `end_mesh_rt_update` (or re-parents
  the node) to re-attach rt instances — those brackets are the existing hooks
  to mirror. Material reassignment on a primitive goes
  through a re-register too — but NO hook exists for it today: reassignment
  is a direct write via `Mesh::get_mutable_primitives()` (material paint
  tool, properties window, glTF parser, asset workflow material swap, material
  preview, bone visualization proxies — i.e. every writer of
  `Mesh_primitive::material` through `get_mutable_primitives()`).
  Requirement: add a material setter / notification on `Mesh` (routed through
  `Scene_host`, R0a) and convert ALL such call sites (grep for
  `get_mutable_primitives`); until every call site is converted,
  `Draw_list_scene` shows a stale material for that primitive. Material *content* edits are handled by
  the wholesale rebuild hook (R1a) triggered from the material edit path (or,
  where no such path exists yet, by a per-frame material-hash check — planner
  decides). Fine-grained per-entry updates are future work; entries must
  never reference primitives by an index that can go stale without one of
  these hooks firing.
- R12b (live per-primitive upload inputs): per-primitive values that are
  uploaded every frame today and edited in place without any hook —
  `Mesh_primitive::lightmap_uv_scale_offset` (lightmap streamer / baker),
  node world transform, per-item color source flags — are read live from the
  `Mesh` / `Mesh_primitive` / `Node` at upload time (R8a), never baked into
  `Draw_list_entry`. Only identity-affecting inputs (R13) are baked.

### 5.3 Draw_list

- R13: Carries everything that is uniform across its entries: the
  primitive-derived shader key components, buffer set (vertex input key, index
  buffer identity, vertex buffer identities), `negative_determinant`,
  blending classification, primitive mode, and mesh layer id (Q3: layer is
  part of draw list identity — entries from different layers never share a
  list) — i.e. the full pipeline/bind state needed at draw time, resolved
  once per list per pass, not per entry.
- R14: Entries are stored contiguously for hot-path iteration.

### 5.4 Draw_list_entry

- R15: Contains only the per-primitive parameters not covered by its
  `Draw_list`: what is needed to (a) cull the entry (world-space AABB) and
  (b) emit its indirect draw command and per-primitive GPU data (index range,
  vertex offsets, transform source, stable material / skin reference — not
  the GPU slot, see R8a — and per-entry flags).
- R16: Layout is value-type and cache friendly: fixed size, no per-entry heap
  indirection on the hot path. References to cold/owning data (the
  `Draw_list_object`) go through indices, not pointers that the hot path must
  dereference.

## 6. Correctness requirements

- C1 (parity): For scenes containing only supported content, images produced
  via `Draw_list_scene` are identical to `Forward_renderer` / `Shadow_renderer`
  output for the same passes for opaque content and shadow maps (order-
  independent). Neither today's renderer nor `Draw_list_scene` depth-sorts
  translucent primitives — today's translucent order is layer/mesh iteration
  order, and persistent lists will differ — so translucent intra-class draw
  order is implementation-defined and pixel-exact translucent parity is NOT
  required. Depth sorting of translucent entries is future work.
- C2: Mirrored transforms (negative determinant) select the front-face-flipped
  pipeline variant per draw list, as today per bucket.
- C3: Visibility/pass filtering semantics (`Item_filter` behavior for the
  supported passes) must be preserved, expressed as draw-time filtering on
  mirrored per-entry flag bits (R7a/R12a). A stale mirror (flag changed on the
  scene item without the `Draw_list_object` update) is a bug in the mirroring
  path, not acceptable divergence.
- C4: Coexistence with `Forward_renderer` in the same frame must not corrupt
  shared state (ring buffers, texture heap, samplers).
- C5 (multiple scene views): several viewports / an XR view rendering the
  same scene in one frame, each with different per-view settings (render
  style, camera, exposure, shader debug, ID-buffer edge method, multiview),
  must all render correctly with no per-frame re-resolution churn. Therefore
  the color environment configuration (R18) may contain ONLY scene-level and
  global inputs (the scene's light partition, the global graphics preset) —
  never per-view inputs. Per-view inputs are either draw-time parameters
  (filter, settings, layers, blending selection, camera data), the R19 view
  config axis (multiview), or per-pass fallback routing (shader debug,
  `EDGE_LINES_FROM_ID`). Verification: two viewports of one scene, one
  desktop + one XR view, one with shader debug on — no re-resolution after
  the first frame.

## 7. Performance requirements

- P1: Steady-state frame with no scene changes: zero bucketing/classification
  work; per-pass CPU cost is iteration + upload + draw submission only.
- P2: Registration cost is O(primitives of the object × draw lists touched);
  draw-list lookup during registration must not be a linear scan over all
  existing lists (unlike today's `Render_bucket::accept()` scan).
- P3: Target scale: thousands of primitives, tens of draw lists, multiple
  passes per frame (N shadow lights + color passes), including XR (72–90 Hz,
  multiview) — the current bottleneck scenario.
- P3a (per-draw entry cap): the binding cap on entries per multi-draw is
  `Primitive_interface::max_primitive_count` (config `renderer.json`, 6000;
  further clamped by `max_uniform_block_size` on non-SSBO devices, since
  `ERHE_DRAW_ID` indexes the `primitives[]` UBO array and
  `Ring_buffer_client::bind` VERIFYs the range fits). `Draw_indirect_buffer`
  ring ranges grow on demand (its `max_draw_count` is unused). A draw list
  larger than the cap — likelier than today because R4 coarsens shadow lists
  — must be drawn as chunks of ≤ cap entries, each with its own primitive
  range + indirect range + multi-draw. Silent truncation is not acceptable.
- P4: Measurable: before/after timing of the passes replaced (existing
  `ERHE_PROFILE` zones or equivalent) demonstrating the win at target scale.

## 8. Open questions (to resolve before planning)

- Q1: RESOLVED — option (b): selection changes go through a
  `Draw_list_object` flag API that updates flag bits in that object's entries
  in place; `draw()` filters on those bits (R12a, R7a).
- Q2: RESOLVED — both levels, with a fixed split: pass *purpose* (color /
  shadow) is registration-time list membership; dynamic per-item state
  (selected / hovered / visible) is a draw-time per-entry flag test (R7a).
  Mirrored bits: the full `Item_flags` word (R12a).
- Q3: RESOLVED — all layers. Mesh layer id is part of draw list identity;
  entries from different layers go to separate draw lists, and `draw()`
  selects layers by selecting lists (R6a, R13).
- Q4: RESOLVED — same pattern as physics/raytrace:
  `Scene_root::register_mesh` / `unregister_mesh` register with
  `Draw_list_scene` alongside `m_raytrace_scene` / `m_physics_world` (R0).
- Q5: RESOLVED — explicit mobility flag in `Draw_list_object_create_info`;
  in the initial scope a transform change on a static-registered object
  asserts (R10a). Review addition: `Scene_root::register_mesh` has no
  mobility source today, so initially everything non-skinned registers as
  dynamic; the flag exists in the API for the future static source.
- Q6: RESOLVED — no culling initially; frustum culling on the entry AABB is
  the first future-work item after the initial renderer works (see §3).
- Q7: RESOLVED — invalidation hook that rebuilds the draw lists. New
  requirement R1a: registration records are kept complete enough to recreate
  all draw lists from scratch. (Review correction: `Mesh_memory` never moves
  allocations at runtime, so the hook's initial clients are material-content
  edits (R12) and future wholesale invalidations, not buffer residency.)
- Q8: RESOLVED — future work; no additional uses in the initial scope, the
  fallback covers them (R5).
- Q9: RESOLVED — option (a): while the ID-buffer edge-line method is enabled,
  the content fill passes fall back to `Forward_renderer`
  (`EDGE_LINES_FROM_ID` stays out of `Draw_list_scene` entirely). Treating it
  as a second baked config remains possible future work if the fallback cost
  hurts in practice.
- Q10: RESOLVED — existing prewarm stays authoritative; both renderers share
  the variant/pipeline caches; draw-list resolution compiles only on miss and
  only off the per-frame hot path (R22).
- Q11: RESOLVED — initial scope asserts if a registered object's determinant
  sign changes at runtime; re-listing on flip is future work (R10b). Review
  additions: registration samples the determinant from the world transform
  (attach ordering); the assert is known to trip on interactive mirroring and
  on reparenting under a mirrored parent — flagged for revisit.
