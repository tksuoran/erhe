# Draw list renderer

Stability: stable

`erhe::scene_renderer::Draw_list_scene` is a scene's persistent,
incrementally maintained rendering-side representation: registering an object
once produces draw lists that later frames reuse, so per-frame CPU cost is
proportional to what changed and to what is drawn rather than to the total
primitive count times the number of passes. It is the third per-domain system
a `Scene_root` owns, beside the physics world and the raytrace scene.

This document states the requirements the renderer meets and the design that
follows from them. Its labels (`G*`, `R*`, `C*`, `P*`, `Q*`) are cited from
source comments and keep their meaning across edits. Two companion documents
own subjects this one only refers to: `doc/erhe/draw_list_material_set.md` owns the
material buffer and texture heap a draw list binds, and
`doc/erhe/draw_list_performance_improvements.md` owns the cached per-primitive
records the draw path copies out.

## 1. Problem statement

`erhe::scene_renderer::Forward_renderer` (color passes) and
`Shadow_renderer::draw_shadow_casters` (shadow maps) recompute everything from
scratch on every call. They remain the fallback path for every pass a draw
list does not express, so this cost is still paid there. With thousands of
primitives in the scene it does not scale, because these are called many times
per frame (shadow map per light / cube face, opaque content, translucent
content, selected / not-selected passes, edge line composition passes, ID
render), and each call repeats work whose inputs did not change since the
previous frame:

- `bucket_primitives()` walks every mesh and every primitive, derives a
  `Shader_key` per primitive, and linearly scans the bucket vector
  (`Render_bucket::accept()`) to place each primitive. This is O(meshes x
  primitives x buckets) per `render()` call, and it runs once per entry of
  `base_render_pipelines` inside each call.
- Bucket contents are identical frame to frame whenever the scene composition
  did not change, yet the buckets are rebuilt and thrown away every call.

## 2. Goals

- G1: Registering an object once produces persistent draw lists; subsequent
  frames reuse them without re-bucketing.
- G2: Per-frame CPU cost of issuing a pass is proportional to the number of
  draw lists and entries drawn in that pass, with cache-friendly iteration.
  "No per-entry pointer chasing" is the goal for static lists; dynamic and
  skinned lists necessarily read per-frame scene state (node world transform,
  joint data) while building their primitive upload.
- G3: Output parity: for supported passes the rendered image is identical to
  what `Forward_renderer::render()` / `Shadow_renderer` produce, and the
  pipeline / state selection per draw list is the same as per bucket. The draw
  call *count* may differ where draw list identity is deliberately finer
  (layer id, R13) or coarser (shadow material coarsening, R4) than bucket
  identity.
- G4: The design leaves room for further optimizations on static objects
  (cached per-primitive GPU data, culling structures) without API changes.

## 3. Scope

### Covered

- Shadow map rendering (triangle fill; all three caster variants: depth-only,
  depth-only + distance technique, point-light cube; material ignored except
  opacity / blending classification).
- Main color rendering (triangle fill, opaque, alpha tested and translucent).

### Not covered

- Shader debug override visualizations.
- Edge line passes (all methods) and other non-`polygon_fill` primitive modes.
- ID render / picking passes.
- Entry-granular mirroring of mesh / primitive / material edits. Edits are
  handled, but coarsely, by re-registering the object or rebuilding (R12).
- GPU-driven culling or draw generation (the design must not preclude it).

### Fallback

`Forward_renderer` (color) and `Shadow_renderer::draw_shadow_casters` (shadow)
are intact and serve every pass that `Draw_list_scene` does not support. Both
fallback paths and `Draw_list_scene` can run in the same frame against the same
scene. The editor setting `use_draw_lists` selects the path at
runtime; ineligible passes always take the fallback.

## 4. Draw list identity

The implementation lives in
`src/erhe/scene_renderer/erhe_scene_renderer/draw_list_scene.*`, `draw_list.*`,
`draw_list_entry.*`, `draw_list_object.*`, `draw_list_key.*` and
`draw_list_renderer.*`.

Bucket identity, which draw list identity mirrors, is defined by
`Render_bucket` / `bucket_primitives()` (`mesh_memory.cpp`):

- `primitive_mode`
- vertex input key (`bucket_vertex_input_key`)
- index buffer identity and vertex buffer identities (pool id + buffer id)
- `Shader_key` hash (derived from material, vertex format, skinned flag, plus
  the pass "environment" key)
- `negative_determinant` flag (mirrored transforms select the front-face
  flipped pipeline variant)
- blending class (opaque, alpha tested or translucent, via
  `Blending_mode_policy`; `erhe::primitive::Blending_class`). A primitive
  without a material classifies as the default material record, opaque,
  in both paths (doc/erhe/scene_renderer.md "Draw order of the blending
  classes").

Mesh layer id is not part of bucket identity (content and controller meshes
share buckets); R13 adds it to draw list identity deliberately (Q3).

The two purposes use different environment keys: the color path builds a full
environment key (light counts, shadow settings, multiview, debug); the shadow
path uses an *empty* environment key plus force masks. Draw list resolution
reproduces this per purpose so shadow resolution hits the same variants
`Shadow_renderer::prewarm_pipelines` warms (R22).

### Environment component inventory - what is baked

`Shader_key` and `Render_parameters` mix primitive-derived inputs (material
features, vertex format, skinned flag, blending mode - trivially bakeable per
draw list) with "environment" inputs. The table inventories every environment
input, with how the editor drives it for the covered passes (shadow map plus
content fill opaque / translucent, from `app_rendering.cpp` /
`composition_pass.cpp` / `shadow_renderer.cpp`), and its bake classification:

| Component | Source in the editor | Changes when | Bake classification |
|---|---|---|---|
| Light counts (6 axes: directional / spot / point x shadowed / not) | `compute_light_layer_partition(lights)` per color `render()` call; depends on each light's type, `cast_shadow`, and `is_active()` (point range > 0) | Light add / remove goes through `Scene_root::register_light`; type / range / cast_shadow are edited by direct member writes with no event (properties window, MCP) | **Scene config** - baked; because there is no reliable edit event, the partition is recomputed (O(lights), trivial) at the start of each color `draw()` and re-resolved only if it differs from the cached one |
| `SHADOW_FILTER`, `SHADOW_BIAS`, `SHADOW_TECHNIQUE`, `SHADOW_DEPTH_BITS` (color path) | Graphics preset (`app_settings`), identical for every pass | User changes the graphics preset | **Global config** - baked, and recomputed and compared alongside the light partition (R18). The shadow path's distance-technique toggle is the same preset input but is modelled as the R4a sub-variant, not as config |
| `SHADER_MULTIVIEW_COUNT` | key value 0 when `views.size()==1` (desktop viewports), N when `views.size()==N>=2` (XR) | Per scene view; desktop mirror plus headset can render the same scene in one frame | **View-config axis** - enumerated up front from the same source prewarm uses (`multiview_view_counts`); a draw list caches a resolved variant per enumerated view config; a not-yet-enumerated config seen at draw time resolves lazily once (R19) |
| `SHADER_DEBUG` | 0 for covered passes (the bone N.V pass has an override - not covered) | Debug UI | **Fixed to 0** - non-zero falls back to `Forward_renderer` |
| Force-enable mask | 0 for content fill; shadow: `VARIANT_DEPTH_ONLY` (directional / spot), `+ VARIANT_SHADOW_DISTANCE` when the distance technique is active, `VARIANT_SHADOW_CUBE` for point-light cube faces (on a cull_none base pipeline); `EDGE_LINES_FROM_ID` toggled onto the fill passes when the ID-buffer edge method is active (fallback while active - Q9); other masks (`SOLID_WIREFRAME`, `EDGE_LINES_CORNER_CAP`, `VARIANT_BRUSH_PREVIEW`) belong to passes that are not covered | Light type per shadow pass; graphics preset; editor settings toggles | **Baked as a shadow sub-variant axis** {depth-only, depth-only+distance, cube} - resolved per shadow list, selected by the caller per shadow pass; `EDGE_LINES_FROM_ID` takes the fallback (Q9) |
| Force-disable mask | Always 0 in the editor | - | **Fixed to 0** |
| `Blending_mode_policy` | `opaque_primitives_only` / `alpha_tested_primitives_only` / `translucent_primitives_only` for fill passes; opaque-only for shadow | Static per pass | **Already baked** - this is the draw list's blending classification (R7) |
| `Item_filter` | Selected / not-selected / visible splits | Selection and visibility change constantly | Not a shader input - see Q1 / Q2 |
| `base_render_pipelines` | Static pass definitions (depth / stencil / raster state) | Never at runtime | Supplied by the caller at draw time; pipeline resolution cached per (draw list x pass) |
| `shader_stages_override`, `color_blend_override` | Only passes that are not covered (selection stencil mask, macOS GL edge lines) | - | **Not supported** - such passes use the fallback |
| `Primitive_interface_settings`, exposure, camera / viewport, light *data*, joint matrices, material contents | Per-pass / per-frame UBO and primitive-buffer contents | Every frame | Not variant-affecting - per-frame upload, orthogonal to draw list identity |

### Environment components affect resolution, not partitioning

The environment key is the shared base that per-primitive `Shader_key::derive`
builds on, so a uniform environment shifts every primitive's key identically:
it can change which variant / pipeline a draw list resolves to, but never which
entries a list contains. Consequence: an environment configuration change (R18)
re-resolves cached pipelines only - it never re-buckets and never touches
entries. This invariance is required going forward (R21).

The only components that interact per-primitive, and therefore *would* affect
partitioning, are:

- `SHADER_DEBUG` x `shader_debug_filter` (per-mesh axis drop) - this is why
  shader debug stays out of scope and fixed to 0 rather than becoming a config
  axis.
- `Blending_mode_policy` x material blending mode - membership, already baked
  as the list's blending classification.
- Force masks in general can coarsen partitioning (merging lists that differ
  only in a forced bit). The covered bits (`VARIANT_DEPTH_ONLY`,
  `VARIANT_SHADOW_DISTANCE`, `VARIANT_SHADOW_CUBE`; the ID-buffer edge method
  uses the fallback, Q9) are set exclusively by force masks -
  `Shader_key::derive` never sets them from a material - so they are guaranteed
  uniform, pipeline-only effects. Any future variant that forces a bit
  materials can also set must be re-analyzed.

Unrelated to environment, but the one identity component that could change at
runtime: `negative_determinant` is transform-driven. A runtime sign flip on a
registered object is reported and asserts in debug (R10b).

Requirements following from this:

- R17: Each draw list caches its fully resolved shader stages
  (`Shader_variant_cache` result) per resolution config, resolved at the
  list's first draw and on configuration change (plus the one-off lazy paths of
  R19 / R4a) - not per frame, not per draw, and never per entry. The cache
  (`Draw_list_resolutions`) belongs to the render phase, keyed by list index
  and `Draw_list::identity`, because the color environment it combines with
  is the render phase's (section 9.3). The *render
  pipeline* additionally depends on inputs known only at draw time
  (`Base_render_pipeline`, render-pass descriptor, color blend);
  `Base_render_pipeline::get_pipeline_for` is a hash-cached lookup keyed on
  those, so per-list pipeline lookup at draw time is one hash probe, not a
  compile. Cache key for the resolved stages: (list identity x purpose
  sub-variant x view config x environment configuration).
- R18: `Draw_list_scene` holds one **environment configuration**
  (`Color_environment`) for the color purpose: light-count partition plus the
  shadow settings of the graphics preset. The shadow purpose has no environment
  configuration: its only varying input, distance technique on / off, is
  expressed as the caller-selected sub-variant of R4a. Changing the
  configuration invalidates and re-resolves the cached stages of the affected
  lists - never their contents (R21). Both parts are recomputed and compared at
  the start of each color `draw()` (`set_color_environment`), because light
  type / range / cast_shadow and the preset are edited by direct member writes
  with no event; the comparison is cheap and needs no message-bus subscription.
- R19: Multiview is a small enumerable axis of the cached resolution
  ({single-view, multiview-N}), not a rebuild trigger: a desktop viewport and an
  XR view of the same scene render in the same frame. The set of view configs is
  enumerated up front from the same source prewarm uses (the
  `multiview_view_counts` list) and resolved together at a list's first color
  draw; a config not in that set encountered at draw time resolves lazily once
  and is added to the set. This lazy path and R4a's first-use shadow sub-variant resolution are the
  only two exceptions to R20; both are by construction one-offs, not steady
  state.
- R20: Steady state (no configuration change, no new view config, no first-use
  shadow sub-variant): `draw()` performs zero `Shader_key` derivation and zero
  `Shader_variant_cache` lookups; it uses the cached stages directly and one
  hash probe per list for the pipeline (R17).
- R21 (environment invariance): Draw list contents - the set of draw lists,
  their identities, and their entries - are invariant under every environment
  change (light-set changes, graphics preset changes, view config, editor
  setting toggles such as the ID-buffer edge method). Environment changes may
  only invalidate and re-resolve cached variants / pipelines (R17); they never
  add, remove, move, or re-partition entries. Entries change only on scene-side
  events: registration / unregistration (R1 / R2), re-registration on mesh /
  material edits (R12), and the rebuild hook (R1a); flag changes update entry
  state in place (R12a).
  Corollary: any future variant axis or forced shader bit that would break this
  invariance (per-primitive interaction, as analyzed in this section) must be
  redesigned or kept on the `Forward_renderer` fallback - it must not be bolted
  onto `Draw_list_scene`.
- R22 (variant compilation, resolves Q10): `Forward_renderer` and
  `Draw_list_scene` share the same `Shader_variant_cache` and pipeline caches -
  the same variant is never compiled twice because two renderers want it. The
  prewarm paths (`prewarm_standard_variants`,
  `Shadow_renderer::prewarm_pipelines`) are authoritative: they cover the
  fallback passes regardless, and with a shared cache draw-list resolution
  (R17) is a cache hit for prewarmed content - provided the keys match.
  Consequently, wherever draw list identity intentionally differs from bucket
  identity, the corresponding prewarm warms the draw-list key too (concretely:
  shadow prewarm warms the coarsened shadow key of R4). On a cache miss (an
  unwarmed variant appears via a new list or a config change), compilation
  happens at that list's first draw after it - a one-off, not steady state.
  A list whose variant cannot be resolved is skipped with a warning, exactly
  like the "No shader variant for bucket" behavior of the fallback.

## 5. Components and requirements

Ownership chain: `Scene_root` owns one `Draw_list_scene` (unique_ptr, same
pattern as `m_physics_world`). `Draw_list_scene` owns its `Draw_list_object`s
and `Draw_list`s. A `Draw_list` owns its `Draw_list_entry`s.

### 5.1 Draw_list_scene

Registration:

- R0 (registration pattern, resolves Q4): registration follows the
  physics / raytrace pattern in `Scene_root`: `Scene_root::register_mesh` /
  `unregister_mesh` (driven by the item-host attach / detach path, as with
  `attach_rt_to_scene` / `Node_physics_system`) also register / unregister the mesh
  with `Draw_list_scene`. There are no separate editor-side call sites.
- R0a (library layering): `Mesh` lives in `erhe::scene`, which does not and
  must not link `erhe::scene_renderer`. Every scene-to-draw-list notification
  named in this document (registration, primitive-set change, material
  reassignment, flag mirroring, transform update) therefore goes through the
  `erhe::scene::Scene_host` interface (`scene_host.hpp`) - virtuals alongside
  `register_mesh` / `unregister_mesh` - implemented by `Scene_root`, which owns
  the `Draw_list_scene`. `Mesh` calls its host; it never sees
  `Draw_list_scene`. This is how `attach_rt_to_scene` and `Node_physics_system` are
  reached.
- R1: `register` function taking `Draw_list_object_create_info`. For each
  primitive of the object it determines the draw list(s) the primitive belongs
  to (creating draw lists on demand), and appends a `Draw_list_entry` per
  (primitive x draw list). The classification mirrors `bucket_primitives()`
  (same skips: null primitive, null renderable mesh, zero index count; the
  same blending classification, `erhe::primitive::get_blending_class()`,
  which classifies a primitive without a material as opaque).
- R1a (rebuildability, resolves Q7): `Draw_list_scene` retains, for every
  registered object, a record sufficient to re-run its classification from
  scratch (the `Draw_list_object` keeps the create-info-level inputs, not just
  the resulting entries). An explicit invalidation hook drops and rebuilds all
  draw lists from these records. Its clients are material *content* edits that
  change list identity (R12) and any future wholesale invalidation of baked
  state. `Mesh_memory` / `Buffer_pool` never *move* existing allocations at
  runtime - pools only append blocks - so pool-level residency churn is not a
  client of this hook. A live `Primitive`'s `Buffer_mesh` can however be
  replaced in place: `Primitive_render_shape::commit_geometry_buffer_mesh()`
  and `make_buffer_mesh()` move-assign a new `Buffer_mesh` over
  `m_renderable_mesh` (freeing the old ranges) on attached, registered meshes
  in ordinary use (deferred glTF / prefab finalize via
  `async_raytrace_kickoff_operation`). That path calls
  `Mesh::update_rt_primitives()`, which is why R12 hangs the re-register hook
  there: the per-object re-register, not this wholesale hook, is what keeps
  baked buffer ranges valid.
- R1b (dependencies): classification needs `Mesh_memory`
  (`get_vertex_input` for the vertex format that feeds `Shader_key::derive`),
  resolution needs `Shader_variant_cache`, and records need
  `Primitive_interface`; `Scene_root` has access to none of them on its own.
  They are injected through `Draw_list_scene_dependencies` when
  `Draw_list_scene` is created. A `Scene_root` constructed without them has no
  `Draw_list_scene` (`m_draw_list_scene == nullptr`); registration and flag
  mirroring are no-ops in that case, mirroring how `enable_physics == false`
  leaves `m_physics_world` null. The null graphics backend is still a `Device`
  with a `Mesh_memory`; it gets a `Draw_list_scene` like any other backend and
  stays in sync. The preview roots (material / brush previews) and the tools
  root deliberately have none: they are tiny scenes. Their passes take the
  fallback through the null check in the routing rule.
- R2: `unregister` function that removes all entries belonging to an object.
  Removal does not invalidate the draw lists' suitability for hot-path
  iteration (compaction is an implementation choice; stale entries are never
  drawn).
- R3: A primitive is added to every draw list variant it participates in (one
  entry in a color list and one in a shadow list, for example).

Variants - each registered primitive is classified along two axes:

- Purpose: main color | shadow map.
- Mobility: static (transform never changes; enables further optimizations) |
  dynamic (transform can change) | skinned.

giving six variants:

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
  lists. This is a deliberate coarsening relative to shadow buckets (which
  derive the key from the full material even though the position-only vertex
  path ignores those bits). Concretely, the shadow list *shader key* is exactly
  {`USE_SKINNING`, the forced VARIANT bits of the sub-variant} - no
  material-derived bits at all, including the material-gated
  `USE_VERTEX_VARYING_*` bits, which `ERHE_VARIANT_POSITION_PASS` ignores.
  `Shader_variant_cache` is keyed on `Shader_key` only - the vertex format is
  used just at first compile (attribute locations are sequential per format) -
  so the coarsened key yields one variant per {USE_SKINNING} regardless of
  format. That is correct only because position is location 0 in every mesh
  format and joints / weights sit at the same locations in the skinned formats;
  the shadow prewarm asserts it. Shadow list *identity* additionally keeps
  `negative_determinant`, buffer set, layer id and blending classification
  (opaque-only membership, as the shadow pass uses `opaque_primitives_only`).
  `Shadow_renderer::prewarm_pipelines` builds this identical coarsened key
  (R22).
- R4a: Shadow lists carry a resolved-stages sub-variant axis {depth-only,
  depth-only + distance, cube} (see the inventory table); `draw()` for the
  shadow purpose selects the sub-variant per pass, and the caller supplies the
  matching base pipeline and color-blend state exactly as
  `Shadow_renderer::render` does. The shadow prewarm warms depth-only;
  distance and cube compile on first use, because a sub-variant's stages are
  resolved on the first `draw()` that selects it (one-off, the same carve-out
  as R19's lazy view config), so unused sub-variants are never compiled.
- R5: The variant set is extensible (edge lines, ID render, debug
  visualizations) without reworking registration. Passes not covered by a
  variant fall back to `Forward_renderer` / `Shadow_renderer`.

Drawing:

- R6: `draw` function that renders exactly one purpose variant selected by the
  caller, into a caller-provided render encoder / pass (all mobility classes of
  that purpose in one call - the split by mobility exists for update cost and
  further optimization, not for pass structure).
- R6a (layers, resolves Q3): `Draw_list_scene` covers all mesh layers.
  `draw()` additionally takes the set of mesh layer ids to include (passes draw
  layer subsets: content plus controller for content fill, rendertarget alone
  for the rendertarget pass). Because layer id is part of draw list identity
  (R13), layer selection selects whole lists - it is never a per-entry test.
- R7 (blending class selection): `draw()` takes a blending-class selector
  (`Draw_blending_selection`) - opaque only, alpha tested only, translucent
  only, or all - because the editor's opaque, alpha-tested and translucent
  content fills are separate composition passes with other passes (edge
  lines, sky, grid) in between, and the rendertarget pass uses `allow_all`.
  When all are selected in one call, the classes are drawn in
  `erhe::primitive::blending_class_draw_order`: all opaque lists, then all
  alpha-tested lists, then all translucent lists. Draw order within a class
  is implementation-defined (see C1).
- R7a (draw-time flag filtering, resolves Q1 / Q2): `draw()` accepts a filter
  with `Item_filter` semantics (require-set / require-clear bit masks)
  evaluated against each entry's mirrored flag bits (R12a) while the indirect
  draw commands are built. Entries rejected by the filter emit no draw command
  but remain in their lists. This is how the selected / not-selected / visible
  pass splits are expressed: same lists, different filter per pass.
- R8: Per draw list, drawing performs: fetch the cached pipeline (R17 / R20 -
  resolved earlier via `Shader_variant_cache` and
  `Base_render_pipeline::get_pipeline_for`), bind index / vertex buffers, fill
  and bind per-primitive and draw-indirect ranges, issue one
  `multi_draw_indexed_primitives_indirect`. This mirrors the per-bucket body of
  `Forward_renderer::render()` minus the per-call key derivation and cache
  lookups.
- R8a (GPU buffer ownership and bind contract): `Draw_list_scene` owns its own
  **material** GPU state - a `Material_set`, and through it the material buffer
  and the texture heap (`doc/erhe/draw_list_material_set.md`) - and no other GPU
  buffers. The per-pass Camera / Light / Joint update and bind sequence is the
  responsibility of the owning renderer (`Draw_list_renderer` for color,
  `Shadow_renderer` for shadow), and `draw()` is invoked *inside* that
  sequence with the renderer's `Primitive_buffer` and `Draw_indirect_buffer`
  supplied for filling the per-primitive and indirect ranges.
  `Primitive_buffer::update` and `Draw_indirect_buffer::update` have
  entry-based overloads alongside the `Render_bucket` ones. The pass binds and
  unbinds the material set it is handed, but never creates, updates or resets
  one: that happens once per frame, before any pass.
  Consequence for R15, **joint slots**: `joint_buffer_index` is assigned
  per `Joint_poses::capture` call, so the cached record's `base_joint_index`
  follows it: `Draw_list_scene::publish()` runs after the frame's capture, in
  the prepare phase, rewrites the records of every skinned object whose skin
  moved to another slot, and only then copies the lists for the render
  phase, which reads no `Mesh::skin`. **Material slots are stable**, and deliberately so: a material's
  slot is a property of the `Material_set` that issued it, stable for as long
  as anything references the material in that set, so it *is* baked into the
  cached record and read from there. A slot assigned per renderer call would
  mean "slot 7" names different materials depending on which pass wrote last,
  which is the defect `doc/erhe/draw_list_material_set.md` exists to make
  unrepresentable.
- R9: Frame-varying per-entry GPU data (primitive transforms, joint matrices)
  is uploaded per frame for dynamic / skinned lists. Static lists are
  structured so a future change can skip or cache this upload (G4); the
  per-list `primitive_records` block
  (`doc/erhe/draw_list_performance_improvements.md`) is the upload source such a
  change would use.

### 5.2 Draw_list_object

- R10: Represents one registered scene object (mesh) inside `Draw_list_scene`
  and keeps the mesh and its primitives alive (owning references) for as long
  as it is registered.
- R10a (mobility classification, resolves Q5): static / dynamic mobility is an
  explicit flag in `Draw_list_object_create_info`, supplied by the caller at
  registration; skinned means `mesh->skin != nullptr` (the shader key's
  `USE_SKINNING` additionally requires joint vertex attributes, so a skinned
  list may still resolve a non-skinning variant - that is a key matter, not a
  mobility matter). Because registration happens inside
  `Scene_root::register_mesh` (R0), which has no mobility information, and
  there is no static item flag, every non-skinned mesh registers as
  **dynamic**. The API carries the flag so a static classification source (item
  flag, asset metadata) can be added without an API change. A transform change
  on an object registered as static is a bug and asserts in debug.
- R10b (determinant stability, resolves Q11): `negative_determinant` is baked
  into list identity. Registration samples it from the node's current world
  transform (`world_from_node()` determinant), not from the mesh's
  `Item_flags::negative_determinant` bit: the mesh's own parenting calls
  `handle_item_host_update` (and thus `register_mesh`) *before*
  `handle_node_transform_update` (which sets the flag), so a mesh attached
  under a mirrored parent would otherwise register with the flag clear and flip
  one call later. A runtime determinant sign change on a registered object is
  not supported: `flush_pending()` compares the observed bit against the
  registered value, logs an error and asserts in debug. Known cases that trip
  it: mirroring an object by negative scale interactively; reparenting a node
  under a mirrored parent within the same host (transform update only, no
  re-registration); and a cross-host reparent under a mirrored ancestor
  (`Node::handle_parent_update` registers before `update_world_from_node()`,
  and descendants' world transforms refresh only in the queued propagation
  pass, so registration can sample a stale determinant there).
- R11: Knows which entries in which draw lists belong to it, so unregistration
  and incremental updates are O(entries of this object), not a scan of all draw
  lists.
- R12a (flag mirroring, resolves Q1): dynamic per-item state is mirrored into
  the draw lists through a `Draw_list_object` API: the caller sets / clears
  flags on the object, and the implementation locates that object's entries
  (via R11) and updates a flag-bits field stored in each entry, in place. Cost
  is O(entries of that object). No entries are added, removed, or moved between
  lists by a flag change (flags are entry *state*, not list *identity*, R21).
  The item hook `Mesh::handle_flag_bits_update` (also used to mirror visibility
  into the raytrace instance) is the mirroring path, and it runs for any change
  to a mirrored bit, not only for `visible`. The mirrored value is the full
  `Item_flags` word, because filters test bits across it (visible, content,
  controller, selected, hovered_in_item_tree, shadow_cast, proxy_hidden, ...),
  so the entry field is 64 bits wide and `Item_filter` applies unchanged.
- R12 (mesh / material edits): registered meshes are not immutable in practice.
  The editor calls `Mesh::set_primitives` / `add_primitive` on attached meshes
  (mesh operations, undo / redo, geometry-graph re-bake, merge, vertex move),
  reassigns materials in place, and edits material contents that change list
  identity (bind texture, BXDF model, blending mode); and a live primitive's
  `Buffer_mesh` (index / vertex ranges, buffer ids) can be replaced in place
  without any primitive-list change (R1a). The object is therefore re-registered
  with `Draw_list_scene` (unregister plus register, O(entries of the object))
  from `Scene_host` virtuals invoked by `Mesh::update_rt_primitives()` (reached
  from `set_primitives`, `add_primitive`, and the deferred buffer-mesh commit
  path) and by `Mesh::clear_primitives()` (which does not call
  `update_rt_primitives` and is used on attached meshes by
  `geometry_graph_mesh.cpp`). For an attached mesh the editor brackets
  `update_rt_primitives` with `Scene_root::begin_mesh_rt_update` /
  `end_mesh_rt_update` (or re-parents the node) to re-attach rt instances;
  those brackets are the analogous hooks. Material reassignment on a primitive
  goes through `Mesh::set_primitive_material`, which is the only writer of
  `Mesh_primitive::material` and notifies the host (D11 in
  `doc/erhe/draw_list_material_set.md` follows that chain to the material set).
  Material *content* edits that change list identity are handled by the
  wholesale rebuild hook (R1a), driven by the per-material identity hash
  `Draw_list_scene::check_material_changes()` compares each frame. Entries never
  reference primitives by an index that can go stale without one of these hooks
  firing.
- R12b (live per-primitive upload inputs): per-primitive values edited in place
  are read live from the `Mesh` / `Mesh_primitive` / `Node` at upload time
  rather than baked into `Draw_list_entry`, unless a hook keeps a cached record
  current. The cached `Draw_list::primitive_records` block
  (`doc/erhe/draw_list_performance_improvements.md`) supersedes the live reads for
  the fields it covers and names the hook that maintains each; material slots
  are baked into the record because they are stable per set (R8a).

### 5.3 Draw_list

- R13: Carries everything that is uniform across its entries: the
  primitive-derived shader key components, buffer set (vertex input key, index
  buffer identity, vertex buffer identities), `negative_determinant`, blending
  classification, primitive mode, and mesh layer id (Q3: layer is part of draw
  list identity - entries from different layers never share a list). That is
  the full pipeline / bind state needed at draw time, resolved once per list
  per pass, not per entry.
- R14: Entries are stored contiguously for hot-path iteration.

### 5.4 Draw_list_entry

- R15: Contains only the per-primitive parameters not covered by its
  `Draw_list`: what is needed to (a) cull the entry (world-space AABB) and (b)
  emit its indirect draw command and per-primitive GPU data (index range,
  vertex offsets, transform source, stable material / skin reference - the
  material *slot* is baked into the cached record, the joint slot is
  rewritten into it by the per-frame publish when it moves; see R8a - and
  per-entry flags).
- R16: Layout is value-type and cache friendly: fixed size, no per-entry heap
  indirection on the hot path. References to cold / owning data (the
  `Draw_list_object`) go through indices, not pointers that the hot path must
  dereference.

## 6. Correctness requirements

- C1 (parity): For scenes containing only supported content, images produced
  via `Draw_list_scene` are identical to `Forward_renderer` /
  `Shadow_renderer` output for the same passes for opaque content and shadow
  maps (order-independent). Neither renderer depth-sorts translucent
  primitives - the fallback's translucent order is layer / mesh iteration
  order, and persistent lists differ - so translucent intra-class draw order is
  implementation-defined and pixel-exact translucent parity is not required.
- C2: Mirrored transforms (negative determinant) select the front-face-flipped
  pipeline variant per draw list, as the fallback does per bucket.
- C3: Visibility / pass filtering semantics (`Item_filter` behavior for the
  supported passes) are preserved, expressed as draw-time filtering on mirrored
  per-entry flag bits (R7a / R12a). A stale mirror (flag changed on the scene
  item without the `Draw_list_object` update) is a bug in the mirroring path,
  not acceptable divergence.
- C4: Coexistence with `Forward_renderer` in the same frame does not corrupt
  shared state (ring buffers, texture heap, samplers).
- C5 (multiple scene views): several viewports or an XR view rendering the same
  scene in one frame, each with different per-view settings (render style,
  camera, exposure, shader debug, ID-buffer edge method, multiview), all render
  correctly with no per-frame re-resolution churn. Therefore the color
  environment configuration (R18) contains only scene-level and global inputs
  (the scene's light partition, the global graphics preset) - never per-view
  inputs. Per-view inputs are either draw-time parameters (filter, settings,
  layers, blending selection, camera data), the R19 view config axis
  (multiview), or per-pass fallback routing (shader debug,
  `EDGE_LINES_FROM_ID`). Verification: two viewports of one scene, one desktop
  plus one XR view, one with shader debug on - no re-resolution after the first
  frame (`get_color_environment_change_count()` and the lazy-resolution counter
  stay put).

## 7. Performance requirements

- P1: Steady-state frame with no scene changes: zero bucketing / classification
  work; per-pass CPU cost is iteration, upload and draw submission only.
- P2: Registration cost is O(primitives of the object x draw lists touched);
  draw-list lookup during registration is a hash lookup, not a linear scan over
  all existing lists (unlike `Render_bucket::accept()`).
- P3: Target scale: thousands of primitives, tens of draw lists, multiple
  passes per frame (N shadow lights plus color passes), including XR (72-90 Hz,
  multiview).
- P3a (per-draw entry cap): the binding cap on entries per multi-draw is
  `Primitive_interface::max_primitive_count` (config `renderer.json`, 6000;
  further clamped by `max_uniform_block_size` on non-SSBO devices, since
  `ERHE_PRIMITIVE_INDEX` indexes the `primitives[]` UBO array and
  `Ring_buffer_client::bind` verifies the range fits). `Draw_indirect_buffer`
  ring ranges grow on demand. A draw list larger than the cap - likelier than a
  bucket because R4 coarsens shadow lists - is drawn as chunks of at most cap
  entries, each with its own primitive range (a shared chunk range holds
  every entry of the chunk, section 9.2 "Record upload"), indirect
  range and multi-draw.
  Silent truncation is not acceptable.
- P4: Measurable: per-pass timing of the passes replaced demonstrates the win
  at target scale (section 10).

## 8. Resolved design questions

- Q1: Selection changes go through a `Draw_list_object` flag API that updates
  flag bits in that object's entries in place; `draw()` filters on those bits
  (R12a, R7a).
- Q2: Both levels, with a fixed split: pass *purpose* (color / shadow) is
  registration-time list membership; dynamic per-item state (selected /
  hovered / visible) is a draw-time per-entry flag test (R7a). Mirrored bits:
  the full `Item_flags` word (R12a).
- Q3: All layers. Mesh layer id is part of draw list identity; entries from
  different layers go to separate draw lists, and `draw()` selects layers by
  selecting lists (R6a, R13).
- Q4: Same pattern as physics / raytrace: `Scene_root::register_mesh` /
  `unregister_mesh` register with `Draw_list_scene` alongside
  `m_raytrace_scene` / `m_physics_world` (R0).
- Q5: Explicit mobility flag in `Draw_list_object_create_info`; a transform
  change on a static-registered object asserts (R10a). `Scene_root::
  register_mesh` has no mobility source, so everything non-skinned registers as
  dynamic; the flag exists in the API for a future static source.
- Q6: Frustum culling of color and shadow passes on the entry AABB.
  `Draw_list_entry::world_aabb` is written per object by
  `write_object_bounds()` - at registration, by the transform hook
  (`flush_pending()` rewrites a moved object's records and sets its entries'
  AABB to `Mesh::get_aabb_world()` in the same step) and by the bounds hook
  (`Mesh::notify_primitive_bounds_changed()` ->
  `Scene_host::on_mesh_bounds_changed()` -> `enqueue_bounds_update()` for
  every mesh naming the primitive, which the in-place mesh-component drag
  raises once per edited mesh per frame after growing a primitive's box from
  the positions it wrote) - together with `Draw_list_object::node_abs_extent`,
  the shadow fit's caster vertex rounding input. The same writes keep
  `Draw_list::bounds` (`Draw_list_bounds`) current: the entries' AABB
  centers and half extents as six float arrays parallel to `entries`
  (swap-removed with them, copied by `publish()` with the entries; an entry
  with an invalid AABB stores a negative extent). Culling runs through a
  view set (doc/plans/culling.md): each pass adds its inward-facing planes
  with `Draw_list_scene::add_cull_view(frame_index, purpose, planes)`
  (render phase) and passes the returned `Cull_view` as
  `Draw_color_parameters::cull_view` / `Draw_shadow_parameters::cull_view`;
  empty planes give `Cull_view::none` (no culling). A view with the same
  purpose and bitwise the same planes as one already in the frame's set is
  shared, so the color passes of one viewport cull through one view, and the
  spot and point shadow passes of several viewports' shadow nodes through
  one view each. Every view of a frame is added before the frame's
  classification, which runs once before the first draw of the frame
  (doc/plans/culling.md phase 6a); the editor adds them from the rendergraph
  nodes' pre-execute hooks (doc/erhe/rendergraph.md "Pre-execute hook"),
  outside any render pass, before any node executes.
  `Draw_list_renderer::add_color_cull_view(draw_list_scene, frame_index,
  Color_cull_view_parameters)` adds a single-view color pass's frustum (from
  its `clip_from_world`, as `Camera_buffer` builds it; multiview gives
  `Cull_view::none`) and the caller passes the view as
  `Draw_list_renderer::Render_parameters::cull_view` (the editor:
  `Composer::add_cull_views()` from the viewport and scene image nodes'
  hooks, from the frame's render context the node's execute draws with
  (`get_color_view_parameters()`, the views and depth conventions both the
  cull view and the draw's `Base_render_parameters` take), one view per
  draw list scene, kept in `Composition_pass_prepared::cull_view`; the
  headset's node hook adds one view for its multiview or per-eye color
  passes, the padded union of the eye frusta, and its passes take it
  through `Render_context::immediate_cull_view`, doc/plans/culling.md
  "Headset passes").
  `Shadow_renderer::fit()` fits the light projections and adds every pass
  `Shadow_renderer::draw()` draws later (the editor: `Shadow_render_node`'s
  hook fits, its execute draws): each 2D pass's light frustum (the pass's own
  `clip_from_world`; a depth-clamped directional pass keeps casters beyond
  its depth planes, so it culls on the four side planes only) and the six
  cube faces of each shadowed point light
  (`add_point_light_cull_views(frame_index, purpose, Cull_sphere)`, six
  consecutive views in the face order +X -X +Y -Y +Z -Z, shared by a light
  with bitwise the same sphere): a face's view is the light's range sphere
  (a caster wholly beyond the light's range is beyond its reach, where the
  range attenuation is zero), then the face's octant around the light (the
  90-degree pyramid around the face axis, which the face's frustum is; the
  near plane is not tested). `classify_frame_cull_views(frame_index,
  command_buffer)` classifies every entry of every published list of the
  views' purposes against every view of the frame's set, once per frame
  (every scene is built with a `Cull_frame_registry`
  (`Draw_list_scene_create_info::cull_frame_registry`, required) and
  registers with it at its first `add_cull_view()` of a frame;
  `Cull_frame_registry::classify_frame()` classifies every registered scene,
  the GPU dispatch only for a scene whose slot records `gpu`, which needs
  its `Gpu_cull_classifier`; the editor runs it as the rendergraph's
  pre-execute callback), with the views outside and the entries inside: per view, each test is one
  branch-free lane-wise loop over the SoA bounds that narrows a per-entry
  inside flag - the range sphere (squared distance from the center to the
  box against the squared radius, from the per-axis gaps `d_k = max(|c_k -
  l_k| - e_k, 0)`; the six faces of a point light are consecutive and reuse
  the result and the gaps, so they test the sphere once per entry), then
  for a cube face the octant (face +X keeps a box where `(c_x - l_x) + e_x
  >= max(d_y, d_z)`, face -X where `e_x - (c_x - l_x) >= max(d_y, d_z)`,
  the other axes alike; exact for the octant, so a rejected box covers no
  texel of the face), for any other view each plane (`d = n . c + w + |n| .
  e`, the expressions and evaluation order of
  `erhe::math::aabb_in_convex_volume()`; planes only, conservative, so an
  infinite reverse-Z far plane cannot reject) - and a last loop ORs the
  view's bit into one `uint64_t` per entry per group of 64 views and sums
  the view's survivors (entries with the bit set) over the classified
  lists. The lane loops are free functions over raw arrays in
  `cull_lanes.hpp` (library-internal), tested deviceless by
  `erhe_scene_renderer_tests` (`test_cull_lanes.cpp`: the octant keeps
  every box the face frustum's planes keep, the sphere test matches the
  box-sphere distance). A view added after the frame's classification, and
  a draw naming a view the classification did not cover, are defects
  (verified), as is a frame whose views were never classified (the next
  frame's first `add_cull_view()` verifies).
  `has_cull_view_survivors(frame_index, view)` reads that sum: a view without
  survivors draws nothing whatever its pass's filter, so
  `Shadow_renderer` begins an empty cube face's render pass for its clear
  and skips the rest of the face. The loops compile to SIMD code without
  intrinsics.
  **GPU classification** (doc/plans/culling.md phase 5): the same masks are
  also computed by a compute pass when the frame's slot records
  `Cull_classification::gpu`. `publish(frame_index, frames_in_flight,
  classification)` records the classification in the slot (the editor
  passes its `culling_classification` setting from the frame's
  `Render_settings` snapshot; a device without the capability, or a scene
  built without `Draw_list_scene_create_info::gpu_cull_classifier`, records
  `cpu`, logged once) and, under `gpu`, keeps the slot's GPU bounds buffer
  current: the published lists back to back (each list's first entry in
  `entry_offsets`), two `vec4` per entry, `(center, 0)` and `(extent,
  purpose)`, the floats the CPU lanes read, a skinned list's entries without
  bounds (negative extent). Each frame slot owns one host-visible buffer
  (the slot's previous frame retired before its prepare phase, which
  `Device::acquire_frame_slot()` waits for), and a publish writes only the
  lists whose serial or offset differs from what that buffer holds (the
  per-list serial `copy_to_slot()` compares; `get_gpu_bounds_write_count()`
  counts the list ranges written), so a frame in which nothing changed
  writes nothing and a drag writes the moved object's lists. The device-wide
  `Gpu_cull_classifier` owns the pipelines of `res/shaders/cull_classify.comp`
  and `res/shaders/cull_compact.comp` and the ring buffer clients; it is
  supported on devices with native multi-draw indirect with a GPU-read draw
  count (`erhe::graphics::Device_info::use_draw_indirect_count`) and
  `Record_indexing::first_instance` (the generated commands name records
  through `first_instance`), so the emulated multi-draw path, MoltenVK (no
  count draw) and the null device classify and write their commands on the
  CPU only; Metal draws the count through an indirect command buffer
  (`doc/erhe/graphics.md` "Indirect count draws"). The rule is the device
  capability, never the backend: GPU-generated commands, shadow and color,
  run only where `Device_info::use_draw_indirect_count` holds (Metal 3,
  Metal 4, KosmicKrisp, desktop Vulkan with `VK_KHR_draw_indirect_count`,
  GL 4.6); elsewhere the `gpu` setting falls back to the CPU classification
  and every pass draws CPU-written commands over the shared records. `classify_frame_cull_views()` records, after the CPU
  classification, the frame's one dispatch per scene into the frame command
  buffer, outside any render pass and before the first draw: the frame's
  view set (eight `vec4` per view, transcribed from the CPU's view tests:
  six planes, the range sphere, and purpose, kind, plane count and cube
  face as small exact floats), one invocation per entry, and one mask
  region (one `uvec2` per entry per group of 64 views, at `group *
  entry_count + entry`). The
  shader runs the CPU lanes' tests in their expression order with every
  intermediate `precise`, so its bits are the CPU's bits.
  **GPU command generation** (doc/plans/culling.md phase 6 and "Open", the
  color passes item): the shadow and color passes draw commands a compute
  pass writes from the GPU masks.
  `Shadow_renderer::fit()` gives the frame's `Shadow_command_selection`
  (`Draw_list_scene::set_shadow_command_selection()`: the caster filter, the
  draw-list layers and its `Primitive_buffer`'s chunk size) with its views.
  Each draw-list color pass gives its `Color_command_selection` with its
  culling view (`Draw_list_renderer::add_color_command_selection()` from
  `Composer::add_cull_views()`, the rendergraph node's pre-execute hook:
  the pass's culling view, filter, layers, blending selection, the
  renderer's `Primitive_buffer` and a `Gpu_command_key` naming the pass and
  scene view); a pass without a culling view (multiview), a frame published
  for the CPU classification and a pass `has_drawable_entries()` rejects
  (nothing passes its filter: the selected-entry passes while nothing is
  selected; `Draw_list_renderer::render()` skips it the same way) get none
  and write CPU commands. `classify_frame_cull_views()` of a `gpu` frame
  with a shadow selection or color selections then builds, after the
  classification dispatch, a job table on the CPU: one job per shadow view
  of the frame and per chunk of each published, non-empty shadow list the
  shadow layers select, then per color selection, per published non-empty
  color list its blending classes (in `blending_class_draw_order`, the
  order `draw_color()` draws) and layers select, per chunk. A job holds its
  view and the index of its `Item_filter` in the frame's filter table (16
  bits each in one word; at most `Gpu_cull_classifier::max_job_filters`
  distinct filters, verified), first entry, entry count and its own dense
  command range, at a multiple of 32 commands. It
  records the two passes of `cull_compact.comp` after a storage barrier
  (classification writes -> compaction reads), one storage barrier between
  them, and a command and storage barrier after them (compaction writes ->
  indirect command and count reads, and the prepare steps' reads). With the
  job table it builds the frame's jobs as `erhe::graphics` indirect count
  draw jobs (command offset, chunk entry count, the index of its list's index
  buffer in a per-frame table of the distinct index buffers) and, per view,
  its consecutive jobs. Each shadow pass then prepares only its view's jobs
  (`Draw_list_scene::prepare_gpu_shadow_pass()`, called by
  `Shadow_renderer::draw()` before the pass's render pass begins, for drawn
  passes and non-empty cube faces): the jobs rebased to the view's first
  command and count slot, through `Compute_command_encoder::
  prepare_indexed_indirect_count_draws()` in its own compute encoder, on the
  `Draw_list_scene`'s persistent `Indirect_count_draws` (all tables are
  scratch kept with their capacity). On Metal this dispatches the kernel that
  encodes the pass's surviving commands into an indirect command buffer;
  elsewhere it records the parameters. The pass bits pass runs one invocation per
  (job, entry): the job's view bit and the selection's `Item_filter`
  of the job's filter-table entry (transcribed as 32-bit halves) against the
  entry's flag bits, collected
  into the job's pass bit words (command offset / 32 on), each word written
  whole by the workgroup owning its 32 entries. The compaction pass runs one
  workgroup per job: each lane counts the set bits of a consecutive run of
  the job's words, one prefix sum over the lanes gives each lane its first
  command, and the lane writes the commands of its set bits in entry order
  (the entry's `index_count`, `first_index`, `base_vertex`, instance count
  1, `first_instance = entry - chunk begin`), so a job's commands are
  exactly those the CPU writer produces, in the same order (deterministic:
  no atomics on the output, and every pass rasterizes the same primitive
  sequence as on the CPU path); the job's
  draw count goes to its count slot. `publish()` writes, with the GPU bounds and under the same per-list
  serial check, the slot's command templates (two `uvec4` per entry at the
  bounds' offsets: `index_count, first_index, base_vertex, 0` and the
  entry's flag bits), the fields the CPU writer reads from the entry. The
  job table is a CPU-write ring range per frame, the pass bits, commands
  and counts are device ring ranges per frame (released at the dispatch, kept by the
  ring until the frame retired); the job vector, its CPU side (view,
  filter, list, chunk, color selection), the filter table and the per
  (shadow view, list) and per (color selection, list) first-job lookups
  are render-phase scratch kept with their capacity. The color jobs are one
  consecutive run after the shadow jobs, prepared right after the
  compaction, outside any render pass, as one set of the scene's second
  `Indirect_count_draws` (a view's color passes draw inside one render
  pass, which no prepare may interrupt, so they share one prepare per
  frame); `draw_color()` with a selection id of such a frame verifies the
  selection is its own (view, filter, layers, blending, buffer) and draws
  each chunk with `multi_draw_indexed_primitives_indirect_count(color set,
  job - first color job)` over the chunk's shared records, as the shadow
  passes do below.
  `draw_shadow()` with a culling view on such a frame verifies its filter,
  layers and chunk size are the selection's and draws each chunk with
  `multi_draw_indexed_primitives_indirect_count(set, job - first job of the
  view)` of the pass's set (the job's command range and count slot,
  `max_draw_count` = the chunk's entry count; the draw binds the job's
  index buffer, at offset 0
  as the CPU path's draws do, and the command buffer itself), binding the
  chunk's shared records as before; no `Draw_indirect_buffer::update()`
  runs for it, and no per-entry decision: every chunk of a drawn list is one
  count draw (a zero count draws nothing), the list's pipeline and vertex
  buffers bound once. The shadow pipelines are created with
  `Indirect_count_draw_support::enabled` (Metal's
  `supportIndirectCommandBuffers`), and so are the color pipelines
  `draw_color()` resolves (`get_pipeline_for(..., indirect_count_draws)`),
  which the Metal backends allow because they bind no texture or sampler
  directly to a fragment function (`doc/erhe/metal_backend.md` "Two
  Sampler Binding Paths"). A job reserves its chunk's whole entry count in
  the indirect command buffer, so preparing per shadow pass sizes the
  shadow Metal buffer to the largest single pass (4096 - 5120 commands,
  2.9 - 3.6 MB per frame in flight on Bistro), and the color set to all
  color jobs of the frame (measured per frame in flight: Bistro, one
  viewport plus one `render_scene_image` view, 10240 commands, 7.1 MB;
  heavy in four view with ten entries selected, 11264 commands, 7.8 MB;
  the indirect command buffer's growth is logged). The compaction also
  writes, per job, the chunk's entries passing the filter;
  every `gpu` frame copies the count buffer to host memory, and the
  classification of the frame that reuses its frame slot (the frame retired)
  decodes it into per-view drawn / filtered counts
  (`get_retired_gpu_shadow_counts()`). The copy lands in a persistent
  host-readable buffer of the `Draw_list_scene`, one per frame slot, outside
  the ring buffers (a ring range is reclaimed once its acquire frame
  completes, so it cannot hold bytes until a later frame reads them); each
  slot's buffer grows to its largest count copy and is only regrown when no
  readback is pending in it. `Shadow_renderer::Cull_views` keeps
  the views of the caller's recent fits (`fitted_frames`), and `draw()`
  fills the entry and culled counts of its `Shadow_draw_statistics` from the retired frame's counts by that
  frame's passes (`gpu_counts_frame_index`; frames in flight late; MCP
  `get_scene_lights` reports the frame), the draw list and draw call counts
  being the frame's own. The color passes' counts come the same way per
  selection (`get_retired_gpu_color_counts(key)`): `Composition_pass`
  reports them in `Composition_pass_stats` with
  `draw_list_counts_frame_index`, the retired frame (MCP
  `get_composition_passes` `last_draw_list_counts_frame_index`; 0 for counts
  of the frame itself). The CPU lanes still run on a `gpu` frame:
  `has_cull_view_survivors()` still skips empty cube faces. A frame without
  a shadow selection (no shadow renderer drew the scene) draws its shadow
  passes with CPU commands, and a renderer whose `Draw_indirect_buffer` has
  its debug index count clamp on (`is_index_count_clamped()`; the GPU
  commands carry the raw counts; refused and logged once) gives no
  selection, so its passes write CPU commands. The dispatches index their workgroups flat and split the count
  into y past the device's x limit (`max_compute_workgroup_count`,
  verified).
  `request_cull_classification_check()` / `poll_cull_classification_check()`
  (MCP `get_cull_classification_check`) copy one frame's mask region, and
  its command generation's counts and commands, to host memory (after a
  transfer barrier), compare every bit with the same frame's CPU masks and,
  per job, the GPU count and the commands up to it with what the CPU writer
  produces from those masks (captured at the generation); a new request
  replaces an earlier one (its pending readbacks belong to an older
  generation and are dropped); one `Gpu_timer` times the classification
  dispatch, another the compaction.
  **Occlusion test** (doc/plans/occlusion_culling.md): with
  `publish(..., Occlusion_culling::on, ...)` on a frame whose effective
  classification is `gpu` and whose `Gpu_cull_classifier` supports it
  (`is_occlusion_supported()`: `Device_info::fragment_stores_and_atomics`
  and the pipeline of `res/shaders/occlusion_test.vert/.frag` built; refused,
  logged once: the slot records off), a color view made an occlusion view
  with `add_occlusion_view(frame_index, cull_view, near_plane, key)` (the
  render phase, before the classification; the editor:
  `Draw_list_renderer::add_color_occlusion_view()` from
  `Composer::add_cull_views()`, near plane from the view's clip_from_world,
  `Draw_list_renderer::get_color_clip_from_world()`) gets one visibility bit
  per entry, in the mask layout (`uvec2` per (group, entry), bit = the
  view's index). The classification dispatch writes, for the frame's
  occlusion views (view record words 8 and 9: the near plane, the flag and
  the previous frame's view; header `z` and `w`), three regions of the
  frame slot's occlusion buffer: the test words (a bounded color entry
  inside the view's frustum whose box, grown by 2^-10 of its coordinate
  magnitude, `erhe_occlusion_bounds.glsl`, lies wholly beyond the near
  plane), the result words, preset to 1 for the color entries the pass does
  not test (no bounds or skinned, a box reaching the near plane - a camera
  inside the box among them -, outside the frustum: unknown, kept visible),
  0 elsewhere, and the culled words below. A storage barrier (compute write
  -> vertex read and fragment read / write) follows.
  `draw_occlusion(Draw_occlusion_parameters)`, inside the view's render
  pass after its depth-writing color passes, draws per entry the three
  faces of its grown box that can face the camera: a template of the faces
  at the positive corner (+X, +Y, +Z: seven corners, 18 indices), mirrored
  per axis toward the camera (`Draw_occlusion_parameters::eye`, homogeneous:
  the position of a perspective camera, the +Z axis with w = 0 of an
  orthographic one; `Draw_list_renderer::get_color_view_eye()`), one
  indexed draw with the slot's uint32 index buffer (the pattern of entry e
  at 7 e, written once per capacity, grown with the slot, logged), so the
  vertex index names the entry and the corner and the post-transform cache
  runs 7 vertex invocations per entry. The vertex shader pushes an entry
  whose test bit is clear outside the clip volume; the pipeline tests depth
  in the device's direction (`greater_or_equal` with reverse depth), writes
  no depth and no color and culls no face (the mirroring varies the
  winding; there are no back faces), and the fragment shader
  (`early_fragment_tests`) ORs the view's bit into the entry's result word,
  with an atomic OR only while a plain read sees the bit clear. **Occlusion
  culling**: the classification of the frame right after an occlusion frame
  (frame N + 1 after N, the published lists' entry layout unchanged:
  `Draw_list_slot::entry_layout_serial`, which registration, unregistration,
  swap-removal and `rebuild_all()` bump; transform and flag changes do not)
  reads, per occlusion view, the same view of frame N (matched by
  `Gpu_command_key`: view indices differ between frames) and clears the
  view's mask bit of an entry inside the frustum that frame N tested and no
  fragment marked; the bits it clears go to the culled words. A view
  without a match, a frame after a gap or a layout change, and the frame
  occlusion is turned on consume nothing (every entry visible). The
  dependency is declared at the consumer: `buffer_barrier(previous slot
  buffer, storage_write | compute_shader | fragment_shader, storage_read |
  compute_shader, Barrier_scope::earlier_submissions)` before the dispatch.
  The culled entries are still tested in N + 1 (their test bit follows the
  frustum), so an entry that becomes visible returns one frame later: one
  frame of lag, a pop-in after a fast camera or object move. The color
  commands generated from the masks and the color passes' drawn / culled
  counts (`Composition_pass_stats`) include the occlusion term; the
  CPU masks do not (`has_cull_view_survivors()` and CPU-written passes stay
  frustum-only). The buffer is one persistent host-visible buffer per frame
  slot (three regions at 256-byte alignment, grown to the slot's
  high-water size, logged), bound by every `gpu` dispatch (a frame without
  occlusion views binds it and writes nothing; a dispatch consuming nothing
  binds a small unread buffer for the previous words);
  `record_frame_end()` (`Cull_frame_registry::record_frame_end()`, the
  editor: before `release_frame_records()`) declares the frame's writes ->
  host read, and the classification of the frame that reuses the slot
  (retired) releases it. Only a frame an occlusion check recorded is read
  back: that classification first decodes its words into per view tested /
  occluded / untested / culled counts (`get_retired_occlusion_counts(key)`,
  frames in flight late); other frames' words are never read on the CPU
  (reading every entry's words back costs milliseconds per frame on large
  scenes, and the GPU classification needs none of it).
  `request_occlusion_check()` / `poll_occlusion_check()` (MCP
  `get_occlusion_check`) add the frame's CPU side: per view the bounded
  entries in the frustum, the entries whose (not grown) box reaches the near
  plane, which must all be marked, and the first occluded entries with
  their mesh names. `get_cull_classification_check` reads the frame's
  culled words back with the masks and compares the CPU masks with the GPU
  masks plus the culled bits (the CPU lanes have no occlusion term), and
  the GPU commands with the CPU writer's after dropping the culled entries;
  a culled bit the CPU did not set is a mismatch.
  The view set, the masks and the per-entry flag scratch are render-phase
  state of the `Draw_list_scene` (render thread), started over by the first
  `add_cull_view()` of each frame and kept with their capacity.
  `draw_list_chunks()` makes one pass decision per entry - the flag filter,
  then the view's mask bit - and the primitive record and draw command
  writers both consume that decision, so every command names its entry's
  record (`ERHE_PRIMITIVE_INDEX`, section 9.2 "Record upload").
  Skinned entries (bounds follow the joints, not the transform hook) and
  entries with an invalid AABB are never culled; a multiview pass culls
  only through a view its caller adds itself (the headset's union view). Culled entries are counted in
  `Draw_statistics::culled_count`: per composition pass in MCP
  `get_composition_passes` (`last_draw_list_culled_count`), per shadow pass
  in `Shadow_draw_statistics` (`Shadow_frame_result::draw_statistics` of the last rendered frame,
  MCP `get_scene_lights` `shadow_draw_list_entry_count` /
  `shadow_draw_list_culled_count`). The shadow frustum fit reads its caster
  and receiver bounds from the same objects instead of transforming every
  content mesh's box per shadow render; a caster is an object with at least
  one shadow entry passing `shadow_caster_filter`. The prepare phase
  gathers them (`Draw_list_scene::prepare_shadow_bounds(selection,
  frame_index, frames_in_flight)`: the entries' AABB, the posed joint
  bounds for a skinned object, the node's world transform and node-space
  extent per caster as `Caster_vertex_extent`) into one `Shadow_bounds`
  slot per frame in flight, and the render phase reads the slot of its
  frame (`get_shadow_bounds(frame_index)`, which verifies the slot was
  prepared for that frame), so the render phase dereferences no mesh or
  node. Every change the gather reads (registration and unregistration,
  entries, flags, the transform and bounds hooks) bumps a serial; a slot
  is gathered again only when its serial or selection is stale, and
  otherwise only its skinned objects' posed bounds are rewritten (a skinned
  object whose bounds turn valid or invalid takes a full gather).
  `get_shadow_bounds_gather_count()` counts the full gathers.
- Q7: An invalidation hook rebuilds the draw lists. R1a keeps registration
  records complete enough to recreate all draw lists from scratch.
  `Mesh_memory` never moves allocations at runtime, so the hook's clients are
  material-content edits (R12) and future wholesale invalidations, not buffer
  residency.
- Q8: No additional uses of the variant set beyond the covered passes; the
  fallback covers them (R5).
- Q9: While the ID-buffer edge-line method is enabled, the content fill passes
  fall back to `Forward_renderer` (`EDGE_LINES_FROM_ID` stays out of
  `Draw_list_scene` entirely). Treating it as a second baked config remains
  possible if the fallback cost hurts in practice.
- Q10: The existing prewarm stays authoritative; both renderers share the
  variant and pipeline caches; draw-list resolution compiles only on a miss and
  only off the per-frame hot path (R22).
- Q11: A registered object's determinant sign change at runtime is reported and
  asserts in debug (R10b). Registration samples the determinant from the world
  transform because of the attach ordering; the assert is known to trip on
  interactive mirroring and on reparenting under a mirrored parent.

## 9. Implementation shape

### 9.1 Identity and keys

`Draw_list_key` (`draw_list_key.hpp`) is hashable and there is one `Draw_list`
per distinct key:

```
Draw_list_key
  purpose             : { color, shadow }
  mobility            : Draw_mobility { static, dynamic, skinned }   // R10a
  layer_id            : erhe::scene::Layer_id                        // R13 / Q3
  blending            : { opaque, translucent }                      // R7; null material -> translucent (R1)
  negative_determinant: bool                                         // R10b, sampled from world_from_node()
  buffer_set          : vertex input key, index buffer id, vertex buffer ids
  primitive_mode      : Primitive_mode (polygon_fill)
  primitive_key       : Shader_key    // primitive-derived ONLY:
                                      //   color : derive(material, vertex_format, skinned) minus environment
                                      //   shadow: {USE_SKINNING} only (R4)
```

Environment is not in the key (R21). The resolution configuration is
`Color_environment` (light partition plus shadow settings, R18), the view config
(multiview count, R19) and the shadow sub-variant {depth_only,
depth_only_distance, cube} (R4a).

Combine rule for color resolution: the resolved key's boolean mask is the
primitive key's mask OR the environment mask; integer axes merge per axis (the
environment sets the light-count / shadow / multiview axes, the primitive key
sets its own; no axis is set by both while `SHADER_DEBUG == 0`); the blending
mode comes from the primitive key. The result equals the bucket key the
fallback derives for the same primitive, which is what makes R22's shared cache
a hit and G3 hold.

### 9.2 Classes

- `Draw_list_entry` (R15 / R16): fixed-size value type holding the owning
  object index, the mesh primitive index, the mirrored `Item_flags` word, the
  index range (index count, first index, base vertex) and the world-space AABB
  (culling, Q6).
- `Draw_list` (R13 / R14): its key, a contiguous vector of entries, the
  contiguous `primitive_records` block that parallels it
  (`doc/erhe/draw_list_performance_improvements.md`), its `identity` (unique
  per list creation) and its change `serial`. The same type is the prepare
  phase's working copy and a frame slot's published copy (section 9.3).
- `Draw_list_resolutions` (R17): the resolved-stages cache of one list (per
  view config for color, per sub-variant for shadow), owned by the render
  phase, one per list index, reset when the list at that index has another
  `identity` (after a rebuild).
- `Draw_list_object` (R10 / R11): the create-info-level inputs kept verbatim for
  rebuild (R1a), the (draw list, entry) locations of its entries, the last
  mirrored flag word, the sampled `negative_determinant`, and its
  `Material_slot_id`s into the draw-list material set.
- `Draw_list_scene` (R0 to R9): the key-to-list map, the object free list with
  stable ids (index plus generation, so an entry's object index never needs
  patching), the pending queue, the `Material_set`, and the register /
  unregister / reregister / flags / rebuild / draw API. Unregister is a
  swap-remove that patches the moved entry's owning object.
- `Draw_list_renderer` (`draw_list_renderer.hpp`) is the color entry point: it
  runs the same camera / light / joint / material prologue a bucket pass runs
  and then calls `draw_color()`. `Shadow_renderer` calls `draw_shadow()` in
  place of `draw_shadow_casters` when it is given a `Draw_list_scene`.

Per list, the draw body skips lists not matching the requested layers or
blending class, fetches the cached stages (resolving lazily only per R19 /
R4a), looks the pipeline up, binds index and vertex buffers, fills the
primitive and indirect ranges through the entry-based overloads (one pass
decision per entry, the flag filter and the culling view bit, which both
writers consume), and issues the multi-draw. Chunking (P3a) is applied
jointly at the `Draw_list_scene` level.

**Record upload.** A command names its record through
`ERHE_PRIMITIVE_INDEX` (`doc/erhe/graphics.md` "Draw records"), and how the
records reach the GPU depends on the device's `Record_indexing`, read once
at construction from `Primitive_interface::record_indexing`:

- Under `Record_indexing::first_instance` every draw-list pass shares its
  records, shadow and color alike: the first `draw_shadow()` /
  `draw_color()` of a frame that draws a chunk of a list uploads the
  records of every entry of the chunk once (`Primitive_buffer::update_records()`,
  one block copy from `Draw_list::primitive_records` plus the default
  `Primitive_interface_settings` color / size patch), and every later pass
  of the frame over that list binds that range; its commands
  (`Draw_record_layout::chunk_entries`) carry `first_instance = entry -
  chunk begin`, so a pass writes only its commands. The 18 shadow passes
  over a shadow list cost one record copy instead of 18, and the color
  passes of several viewports (four view) one per color list.
- The one patch is exact for every pass because no draw-list shader variant
  reads a record's pass-dependent fields: `color` and `size` are read only
  by `standard.vert` under `ERHE_VARIANT_POINTS`, `ERHE_SOLID_WIREFRAME`
  and `ERHE_SHADER_DEBUG != 0` and by `standard.frag` under
  `ERHE_VARIANT_ID_RENDER`, and the draw-list path takes polygon fill passes
  without shader debug only (section 9.4); the passes that need per-pass
  color and size (edge lines, corner points, solid wireframe, the id pass,
  shader debug) take the bucket path with their own records. Every color
  and shadow variant a list resolves verifies the invariant on its key
  (`reads_record_pass_fields()` in `draw_list_scene.cpp`). The per-pass
  settings therefore never reach a shared record: the hovered / active
  flag bits and the constant colors of a color pass change nothing the
  shader reads, so no "per-pass constant block" is needed for sharing.
- The ranges are render-phase state of the `Draw_list_scene` (one
  `Ring_buffer_range` per chunk per list, in vectors kept with their
  capacity), acquired lazily per chunk. The frame's first acquisition
  registers the scene with its `Cull_frame_registry`
  (`add_record_scene()`), and the editor calls
  `Cull_frame_registry::release_frame_records()` once per frame in the
  render phase after the rendergraph (shadow, composition and headset
  passes) executed, before the frame's submit; it calls
  `Draw_list_scene::release_frame_records()` of each registered scene,
  which releases the ranges in the frame that drew them (the release makes
  the ring buffer's sync entry for that frame). Every pass of a frame over
  one list binds the ranges of one `Primitive_buffer` (verified; the shadow
  lists are bound by the `Shadow_renderer`'s, the color lists by the
  `Draw_list_renderer`'s), and a new frame's first `add_cull_view()` or
  shared record verifies the previous frame's records were released. A
  list drawn by one pass uploads every entry of a chunk where a per-pass
  write copied only the passing ones (the trade measured in
  `doc/plans/culling.md` phase 4).
- Under `Record_indexing::draw_position` (null device, devices without base
  instance) every pass writes its own records: per chunk,
  `Primitive_buffer::update(draw_list, begin, end, entry_passes, settings,
  count)` copies the passing entries' cached records into a ring-buffer
  range and patches the pass's color and size, and
  `Draw_indirect_buffer::update(..., Draw_record_layout::packed)` writes one
  command per passing entry (`first_instance` 0, where
  `ERHE_PRIMITIVE_INDEX` is the draw position anyway).
- Shadow and color passes under `Cull_classification::gpu` (doc/plans/
  culling.md phase 6, Q6 "GPU command generation") bind the same shared
  records and draw the commands the GPU generated for their view (shadow)
  or selection (color) and chunk with the count draw: they name the same
  records through the same `first_instance`, so the record upload is
  unchanged and the pass writes no commands.
- Passes on devices without a GPU-read draw count (MoltenVK) draw
  CPU-written commands over the shared records.

### 9.3 Scene-side hooks and the threading contract

`erhe::scene::Scene_host` carries the notification virtuals (R0a), implemented
by `Scene_root` and no-ops when `m_draw_list_scene == nullptr`: mesh
registration and unregistration, primitives changed (from
`Mesh::update_rt_primitives` and `clear_primitives`), flags changed (from
`Mesh::handle_flag_bits_update`), material changed (from
`Mesh::set_primitive_material`), transform changed (from
`Mesh::handle_node_transform_update`) and primitive data changed (from
`Mesh::set_primitive_lightmap_uv_scale_offset`).

**Threading contract (mandatory).** `Mesh::update_rt_primitives()` and
`Mesh::handle_node_transform_update()` - and thus the hooks above - run on
`tf::Executor` worker threads (deferred finalize of
`Async_raytrace_kickoff_operation`, under `item_host_mutex`), while the draws
run on the editor's render thread (R, `doc/editor/editor.md` "Frame phases and
the render thread"), which never takes that mutex, and
`Shader_variant_cache::get` is an unlocked map plus a compile. Therefore **no `Scene_host` hook reads or mutates
draw-list state or resolves variants directly.** Every hook only enqueues an
operation (mesh plus kind: register, unregister, reregister, flags, transform,
refresh) into a mutex-protected pending queue on `Draw_list_scene`;
`Draw_list_scene::flush_pending()` runs on the main thread once per frame in
M2 and performs unregister / register there.

**Published copies.** The lists `flush_pending()` maintains are the prepare
phase's working copy, which the render phase never reads. Once per frame, in
M2, `Draw_list_scene::publish(frame_index, frames_in_flight)` copies them into
the frame-in-flight slot `frame_index % frames_in_flight`, and the render
phase of that frame reads only that slot (`draw_color`, `draw_shadow` and
`has_drawable_entries` take the frame index; the slot verifies it was
published for that frame). So the prepare phase of one frame never writes
what the render phase of another frame reads, and the copies need no lock.
Every mutation of a list's key, entries or records goes through
`edit_draw_list()`, which sets the list's `serial` from a counter shared by
all lists of the scene (never reused, so a list recreated by a rebuild never
matches a stale copy); the slot keeps, per list, the serial it copied, and
`publish()` copies a list only when the serials differ. A frame in which
nothing changed copies nothing, and a change is copied into each slot once
(`get_published_list_copy_count()`, MCP `get_draw_lists`
`published_list_copy_count`). Before the copy, `publish()` rewrites the
joint slot fields of the skinned objects whose skin moved to another joint
slot: the caller captures the frame's joint poses first
(`Joint_poses::capture` assigns the slots), so the published records carry
the frame's `base_joint_index`. The frustum cull reads the list key's
mobility (all entries of a list share it), and `Primitive_buffer::update`
copies the published records, so the render phase reads no `Mesh`, `Node`,
`Skin` or `Draw_list_object` on this path. The shader resolution cache and
the color environment (R17 / R18) are the render phase's own state.

Rules:

- Flush site: the editor's M2, after transform propagation (so registration
  samples current world transforms, R10b) and before the rendergraph; one flush
  per registered scene root per frame covers viewports, shadow nodes and the
  headset node. `App_scenes::flush_draw_lists()` is that loop, and the
  material-set update (`doc/erhe/draw_list_material_set.md` D6) runs right after it.
- Publish site: the editor's M2, after the flush and after the frame's joint
  poses are captured: `App_scenes::snapshot_render_settings()` runs, per scene
  root, `Scene_root::snapshot_render_settings` (joint pose capture),
  `prepare_shadow_bounds` and `publish_draw_lists`, the last under
  `item_host_mutex` (the joint slot rewrite reads the meshes), with the
  frame's culling classification setting (Q6 "GPU classification").
- `flush_pending()` takes the scene root's `item_host_mutex` (lock order:
  item_host_mutex, then pending mutex; swap the queue out under the pending
  mutex, then process under item_host_mutex only) so registration never reads a
  `Buffer_mesh` mid move-assign.
- Ordering edge cases: `flags` for a not-yet-registered mesh is ignored;
  unregister plus register of the same mesh in one queue is normal (mesh
  operations detach and re-attach); teardown drops the queue without processing
  it, because `~Scene_root` may run on a worker holding the last `shared_ptr`.
- The R10b determinant check lives in `flush_pending()` when processing a
  `flags` item: the incoming `negative_determinant` bit is compared with the
  object's registered value.
- Flag updates coalesce (last value wins). `register_object`,
  `unregister_object` and `flush_pending` are main-thread-only and assert the
  owner thread id; `flush_pending` also asserts the prepare phase (or no
  frame), and so do `prepare_shadow_bounds` and `publish`. `draw_color`,
  `draw_shadow`, `has_drawable_entries` and `get_shadow_bounds` assert the
  render thread and the render phase (`r_render`) through
  `erhe::utility::Thread_roles` (`doc/erhe/utility.md`); both asserts are
  no-ops in a program that never sets the thread roles.
- `set_exclude_unlit_from_shadows` is main-thread-only (owner thread). The
  editor calls it on the main thread from the change sites of its
  `exclude_unlit_primitives` setting (the Settings window edit in M2, scene
  root registration); a new instance
  takes the initial value from `Draw_list_scene_create_info::exclude_unlit_from_shadows`.
  A change enqueues its rebuild, which the next `flush_pending` performs ahead
  of its queued ops (`doc/erhe/draw_list_material_set.md` D1d).

This satisfies R17 / R20 / R22: resolution is off the per-frame draw hot path;
inside `draw()` it happens only at a list's first draw and after a
configuration change.

### 9.4 Editor routing

`Composition_pass::render` routes to `Draw_list_renderer` only when all hold:
the effective scene root has a non-null `Draw_list_scene` (R1b);
`primitive_mode == polygon_fill`; the effective `shader_debug` is none;
`shader_stages_override` and `color_blend_override` are null; the shader-key
force-enable and force-disable masks are zero; `EDGE_LINES_FROM_ID` is not
applied this frame (Q9); and the blending-mode policy maps to a
`Blending_selection`. The render-style gate, the appearance-derived
`Primitive_interface_settings`, exposure and all base render parameters are
kept on the routed path. Everything else takes the `render()` fallback (brush,
bone and selection stencil passes). `Shadow_render_node` passes the root's
`Draw_list_scene` and its layer id list to `Shadow_renderer`.

## 10. Measured effect

Per-pass CPU wall time inside `Composition_pass::render()` and
`Shadow_render_node::execute_rendergraph_node()`, over the Niagara bistro scene
(`res/editor/assets/niagara_bistro/bistro.gltf`; 2909 registered objects, 5652
entries, 132 non-empty draw lists), default viewport, nothing selected,
measured through the MCP tools `reset_composition_pass_stats` and
`get_composition_passes` with the setting off and on:

| Pass | build | classic (us) | draw lists (us) |
|---|---|---|---|
| Content fill opaque not selected | Release ninja | 1413 | 272 |
| Content fill selected (0 selected) | Release ninja | 175 | 5.6 |
| Content fill translucent not selected | Release ninja | 501 | 104 |
| Content fill translucent selected | Release ninja | 165 | 1.6 |
| Sum of the four fill passes | Release ninja | 2254 | 383 |
| Shadow node (per exec) | Release ninja | 9450 | 2513 |
| Content fill opaque not selected | Debug VS headless | 50000 | 3250 |
| Sum of the four fill passes | Debug VS headless | 74000 | 5425 |
| Shadow node (per exec) | Debug VS headless | 348000 | 48000 |

The bucketing, `Shader_key` derivation and bucket scan that the classic path
repeats per pass are gone (P1), and a pass whose filter rejects everything
early-outs before the pass prologue. What remains in the draw-list cost is
per-list overhead (pipeline lookup, debug label, ring-buffer acquire per chunk,
indirect command write per entry) rather than per-primitive derivation.

Verification recipe for a re-run, and the checks that go with a change to this
path: headless Vulkan, `capture_screenshot` at 2304x1200 with the frame-time
text masked, comparing the setting off against on for the default scene, with a
mesh selected (selected / not-selected fill passes), after moving the selected
mesh, after creating a material, with a shadow-casting point light (cube
sub-variant), and with two skinned models imported and one of them deleted
(joint slot shift). Prewarm parity (R22) is checked by grepping `logs/log.txt`
for `Shader_variant_cache miss` in the first frames with the setting on.

## Future work

- [plans/draw_list_renderer.md](../plans/draw_list_renderer.md): entry AABBs for the shadow fit, static
  lists, determinant-flip re-listing, translucent sorting and the remaining
  material-set follow-ups.
- [plans/culling.md](../plans/culling.md): one classification of every entry
  against every pass of the frame, SoA bounds published with the lists, then
  the same on the GPU with indirect command generation.
