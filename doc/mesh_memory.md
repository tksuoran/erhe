# Mesh_memory: multi-format support and lazy allocation

## Context

`erhe::scene_renderer::Mesh_memory` today owns three fixed-size GPU vertex
buffers (one per stream of a single hardcoded `Vertex_format`) and an index
buffer, all pre-allocated to their config-specified maximum size at editor
startup (default 128 MB per vertex stream + 64 MB indices = ~448 MB GPU
allocated even on an empty scene). Every primitive in the editor is built
against that single format. The shadow renderer already carries an explicit
`// TODO Multiple vertex buffer bindings` at `shadow_renderer.cpp:197`.

Two related goals drive this change:

1. **Multiple vertex formats.** The editor is about to need at least two
   distinct vertex formats (the existing position+skinning layout, plus a
   position-only layout for static meshes). The format set should be
   open-ended -- `Mesh_memory` should be a registry keyed by `Vertex_format`,
   not a holder of one pre-baked format.
2. **Lazy allocation.** Each format pool should start empty and grow only
   when an allocation request would not fit any existing buffer in the pool.
   This eliminates the big up-front VRAM commitment and lets the editor scale
   from "empty scene" to "huge scene" without the user tuning config knobs.

The full scope of this plan includes reworking `content_wide_line_renderer`
(the only consumer that today binds `Mesh_memory::edge_line_vertex_buffer`
directly).

## Approach

### Where buffer identity lives: `Buffer_range`

Add a non-owning `erhe::graphics::Buffer*` field to
`erhe::primitive::Buffer_range` (`src/erhe/primitive/erhe_primitive/buffer_range.hpp`).
This was chosen over `Buffer_mesh` or `Primitive_render_shape` because:

- `Buffer_range` already abstracts "where this data lives" (offset, count,
  stream). Adding the buffer pointer closes the abstraction so a range is
  self-contained -- the renderer reads buffer + byte_offset off the range
  with no parallel-array lookup into Mesh_memory.
- With multiple buffers per pool, the streams of one `Buffer_mesh` may land
  in *different* GPU buffers (stream 0 in pool buffer #1, stream 1 in pool
  buffer #3). Per-range identity is the only honest representation;
  per-`Buffer_mesh` storage requires parallel arrays and can diverge.
- `Buffer_allocation` already knows its allocator (which knows its buffer),
  but `Free_list_allocator` lives in `erhe_buffer` and must not depend on
  `erhe_graphics`. Putting the back-pointer there leaks the graphics
  dependency into a generic byte-allocator. The pointer belongs in the
  primitive layer, which already includes graphics.
- The `Cpu_buffer_sink` path (Embree raytrace) leaves the field `nullptr`;
  consumers there use the `Cpu_buffer` shared_ptr they already hold.

### New types

- `Vertex_buffer_pool` -- owns `std::vector<Vertex_buffer_block>`, each block
  is `{ erhe::graphics::Buffer, erhe::buffer::Free_list_allocator }`. Provides
  `allocate(stream, count, element_size) -> Buffer_sink_allocation` which
  walks existing blocks first-fit, then creates a new block when none fits.
  Allocation of size > block_size gets a buffer sized exactly to it ("oversize
  exclusive block"). One mutex per pool guards walk-and-grow.
- `Index_buffer_pool` -- analogous, single allocator family (u32 indices).
- `Edge_line_vertex_buffer_pool` -- analogous, hardcoded 2x vec4 layout
  (matches `primitive_builder.cpp:158`).
- `Mesh_memory::Format_pools` -- per-format bundle of
  `{ std::vector<Vertex_buffer_pool> (one per stream), Vertex_input_state,
  Buffer_info, Graphics_buffer_sink }`.
- `erhe::dataformat::Vertex_format_key` -- a `uint32_t` bit mask, one bit
  reserved per attribute slot (position, normal[0]=face, normal[1]=smooth,
  tangent, tex_coord, color, joint_indices, joint_weights, plus four custom
  slots). `compute_vertex_format_key(const Vertex_format&)` ORs in the bit
  for every attribute the format declares. Two formats are equal iff their
  keys are equal. The key assumes canonical element types per attribute
  usage (e.g. position is always vec3 float, joint_indices is always
  vec4 u8) -- which matches how erhe authors formats today; if a future
  format diverges from the canonical type for a usage, that needs a
  dedicated bit (or a wider key) rather than a silent collision.
- `Mesh_memory::get_or_create_format_pools(const Vertex_format&)` --
  registry lookup keyed by `Vertex_format_key`. The registry is a
  `std::unordered_map<uint32_t, Format_pools>`. Required (over pointer
  equality) because `Triangle_soup` carries its own format and the gltf
  importer constructs ad-hoc formats inline (`primitive.cpp:54-59`); the
  bit-mask key collapses any two semantically identical formats to the same
  pool regardless of how they were constructed.

### Renderer batching by buffer-set

Multi-draw indirect (Vulkan `vkCmdDrawIndexedIndirect`, OpenGL
`glMultiDrawElementsIndirect`) requires identical vertex-buffer bindings for
the entire call. This forces grouping. For each `mesh_span` the renderer
buckets primitives by
`Bucket_key{ Vertex_input_state*, vertex_Buffer*[N], index_Buffer* }`, and
emits one `Primitive_buffer::update` + `Draw_indirect_buffer::update` +
`multi_draw_indexed_primitives_indirect` triplet per bucket. With one format
+ one block per pool the bucket count is 1 and overhead vs today is zero.
For forward_renderer, `render_pipeline_states` is filtered per bucket by
matching `Vertex_input_state*` -- pipelines whose vertex_input doesn't match
the bucket are skipped.

### Configuration

New keys in `Mesh_memory_config` (`src/erhe/scene_renderer/definitions/mesh_memory_config.py`):

| Key | Default | Notes |
|-----|---------|-------|
| `vertex_pool_block_size_mb` | 32 | Default size of a freshly grown vertex buffer in any format pool |
| `index_pool_block_size_mb` | 16 | Same, for index pool |
| `edge_line_vertex_pool_block_size_mb` | 8 | Same, for edge-line pool |
| `max_buffers_per_pool` | 64 | Soft cap; allocation fails loud past this |
| `max_format_pools` | 32 | Soft cap on registered formats; guards against asset-driven runaway |

The existing `vertex_buffer_size` and `index_buffer_size` keys are deprecated
(no longer used).

## Step list (smallest landable chunk first)

### Step 0: add `Buffer*` to `Buffer_range`, populate but don't read

Files:
- `src/erhe/primitive/erhe_primitive/buffer_range.hpp` -- forward-declare
  `namespace erhe::graphics { class Buffer; }`, append
  `erhe::graphics::Buffer* buffer{nullptr};`.
- `src/erhe/graphics_buffer_sink/erhe_graphics_buffer_sink/graphics_buffer_sink.cpp`
  -- populate `range.buffer` in `allocate_vertex_buffer`,
  `allocate_index_buffer`, `allocate_edge_line_vertex_buffer`.
- `src/erhe/primitive/erhe_primitive/buffer_sink.cpp` (`Cpu_buffer_sink`) --
  leaves field nullptr; verify no consumer dereferences.
- `src/editor/scene/scene_serialization.cpp` -- audit; the new pointer must
  not be serialized.

Behavior change: none. Field exists and is filled. PR is self-contained.

### Step 1: renderers consult `Buffer_range::buffer`, assert single buffer set

Files:
- `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.cpp`
  (lines 184-260, the `render` body) -- stop reading
  `parameters.vertex_buffer0/1/2`/`index_buffer`; bind from the first
  primitive's ranges. Assert subsequent primitives in the same span share
  the same buffer set (assert fires when Step 4 lands).
- `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp`
  (lines 170-260) -- same.
- `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.hpp`
  (lines 55-60) and `shadow_renderer.hpp` -- mark
  `vertex_buffer0/1/2`/`index_buffer` deprecated/optional in
  `Render_parameters`. Renderer prefers ranges over parameters.

Behavior change: none in single-format scenes (all primitives share buffers).

### Step 2: extract `Vertex_buffer_pool` / `Index_buffer_pool` / `Edge_line_vertex_buffer_pool` types

Single block per pool for now -- behavior unchanged. The point is to move
ownership of `(Buffer + Free_list_allocator)` pairs out of
`Graphics_buffer_sink` and `Mesh_memory` into pool types that the rest of
the codebase can be retargeted to.

Files:
- New: `src/erhe/scene_renderer/erhe_scene_renderer/vertex_buffer_pool.{hpp,cpp}`
  (and the index/edge-line variants -- decide co-located vs separate during
  implementation).
- `src/erhe/graphics_buffer_sink/erhe_graphics_buffer_sink/graphics_buffer_sink.{hpp,cpp}`
  -- sink delegates to the pools instead of owning allocators directly.
- `src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.{hpp,cpp}` --
  owns pools; expose existing buffers via thin getters
  (`get_default_vertex_buffer(stream_index)`) to keep call sites compiling.

### Step 3: make pool growth lazy

Files: same as Step 2, plus
`src/erhe/scene_renderer/definitions/mesh_memory_config.py` for the new
config keys.

`Vertex_buffer_pool::allocate` walks existing blocks; if none fits, creates
a new `Buffer` of `max(block_size, requested_size)`. First allocation grows
the empty pool. `Mesh_memory` constructor body shrinks: it no longer
instantiates buffers at startup; only `vertex_format`, `vertex_input`,
`buffer_info`, and the (empty) pools are initialized.

The ~12 call sites that read
`mesh_memory.vertex_buffer_position`/`vertex_buffer_non_position`/`vertex_buffer_custom`
directly (full list under "Critical files") must move to
`mesh_memory.get_default_vertex_buffer(stream_index)`. The shim returns the
first block of the pool (or `nullptr` if pool empty).

### Step 4: renderer groups by buffer-set

Files:
- `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.cpp` --
  bucketing pass in `render` and `draw_primitives`.
- `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.cpp` -- same
  in `render`.
- `src/erhe/renderer/erhe_renderer/primitive_buffer.{hpp,cpp}` -- if needed,
  accept a sub-span of primitives instead of the full `mesh_span`.
- `src/erhe/renderer/erhe_renderer/draw_indirect_buffer.{hpp,cpp}` -- same.

Bucketing key includes `Vertex_input_state*` so single-format scenes still
use the existing pipeline cache (`shadow_renderer.cpp:87-121`); for forward,
the per-bucket loop filters `render_pipeline_states` by matching
`Vertex_input_state*`.

`Forward_renderer::Render_parameters` and `Shadow_renderer::Render_parameters`
drop `vertex_buffer0/1/2`/`index_buffer` entirely. Call sites in
`composition_pass.cpp:126-128/194-196`, `shadow_render_node.cpp:218-223`
stop filling them. The empty-mesh fullscreen path in `composition_pass.cpp`
needs verification (no draw is issued there, so no binds needed).

The Step 1 assertion is removed once buckets work.

### Step 5: multi-format pool registry

Files:
- `src/erhe/dataformat/erhe_dataformat/vertex_format.{hpp,cpp}` -- add
  `Vertex_format_key` (uint32_t bit-mask with one bit per attribute usage
  slot) and `compute_vertex_format_key(const Vertex_format&)`. Unit-test
  the canonical formats produce the expected keys.
- `src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.{hpp,cpp}` --
  the public surface becomes a registry:
  `get_or_create_format_pools(const Vertex_format&) -> Format_pools&`.
  Internally, an `std::unordered_map<uint32_t, Format_pools>` keyed on
  `compute_vertex_format_key(format)`.
- All callers that today pass `mesh_memory.buffer_info` into the build flow
  -- `asset_browser.cpp:88,294,317`, `scene_builder.cpp:262`,
  `create.cpp:154,253`, `items.cpp:100`, `hotbar.cpp:428`,
  `paint_tool.cpp:336`, `handle_visualizations.cpp:438`,
  `controller_visualization.cpp:53`, `rendertarget_mesh.cpp:162`,
  `material_preview.cpp:75`, `operations_window.cpp:261,551,1063`,
  `scene_serialization.cpp:730` -- now ask for the format they want and
  pass that pool's `Buffer_info`. The shim
  `mesh_memory.default_buffer_info` still returns the editor's main format
  pools so unmigrated callers keep working.

The single index pool and single edge-line pool stay shared across all
formats (indices are u32; edge-line is hardcoded vec4+vec4).

Soft cap `max_format_pools` from config guards against asset-driven runaway.

### Step 6: register a position-only format alongside the existing skinned format

Files:
- `src/editor/editor.cpp:786-815` -- register a second `Vertex_format`
  (position-only on stream 0; non_position and custom unchanged).
  Editor selects which format a mesh is built against based on whether it
  carries skinning data.
- `src/editor/scene/scene_builder.cpp` and other build sites -- pick the
  appropriate format when constructing `Buffer_info` for each primitive.
- Pipelines that render skinned vs static meshes in `app_rendering.cpp`,
  `tools.cpp`, `scene_preview.cpp`, `composition_pass.cpp`,
  `material_preview.cpp` -- each constructs against the matching
  `Vertex_input_state` from its target format. The renderer's bucket loop
  picks the right pipeline per bucket.

This is the first PR where the multi-format machinery is exercised
end-to-end.

### Step 7: rework `content_wide_line_renderer` for pool-sourced edge-line buffers

File: `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.{hpp,cpp}`.

Today the constructor takes `Buffer&` (line 61, .cpp:29) and binds it as an
SSBO at line 306, 486. With a lazy-grown edge-line pool an arbitrary mesh
can land in any block. Change the constructor to accept the
`Edge_line_vertex_buffer_pool&` and emit one compute dispatch per
`(buffer*)` group: walk primitives, group by
`edge_line_vertex_buffer_range.buffer`, rebind the SSBO between groups.
Validate on Quest (OpenGL ES compute path).

## Critical files

Modified:
- `src/erhe/primitive/erhe_primitive/buffer_range.hpp` (Step 0)
- `src/erhe/graphics_buffer_sink/erhe_graphics_buffer_sink/graphics_buffer_sink.{hpp,cpp}` (Steps 0, 2)
- `src/erhe/primitive/erhe_primitive/buffer_sink.cpp` (Step 0; Cpu sink leaves nullptr)
- `src/erhe/scene_renderer/erhe_scene_renderer/mesh_memory.{hpp,cpp}` (Steps 2, 3, 5)
- `src/erhe/scene_renderer/erhe_scene_renderer/forward_renderer.{hpp,cpp}` (Steps 1, 4)
- `src/erhe/scene_renderer/erhe_scene_renderer/shadow_renderer.{hpp,cpp}` (Steps 1, 4)
- `src/erhe/renderer/erhe_renderer/primitive_buffer.{hpp,cpp}` (Step 4 if needed)
- `src/erhe/renderer/erhe_renderer/draw_indirect_buffer.{hpp,cpp}` (Step 4 if needed)
- `src/erhe/scene_renderer/definitions/mesh_memory_config.py` (Step 3)
- `src/erhe/dataformat/erhe_dataformat/vertex_format.{hpp,cpp}` (Step 5; `Vertex_format_key` + `compute_vertex_format_key`)
- `src/erhe/scene_renderer/erhe_scene_renderer/content_wide_line_renderer.{hpp,cpp}` (Step 7)
- `src/editor/editor.cpp` (Steps 3, 6)

Call sites migrating off direct buffer access (Step 3 shim then Step 5+):
- `src/editor/app_rendering.cpp` (~15 lines around 497-803)
- `src/editor/renderers/id_renderer.cpp:64,74,221`
- `src/editor/renderers/composition_pass.cpp:126-128,194-196`
- `src/editor/tools/tools.cpp:44,79,114,130,142,177`
- `src/editor/tools/paint_tool.cpp:336,365,377`
- `src/editor/preview/scene_preview.cpp:43`
- `src/editor/preview/material_preview.cpp:75`
- `src/editor/rendergraph/shadow_render_node.cpp:218-223`
- `src/editor/rendertarget_mesh.cpp:162`
- `src/editor/transform/handle_visualizations.cpp:438`
- `src/editor/xr/controller_visualization.cpp:53`
- `src/editor/scene/scene_builder.cpp:262`, `scene_serialization.cpp:730`
- `src/editor/asset_browser/asset_browser.cpp:88,294,317`
- `src/editor/create/create.cpp:154,253`
- `src/editor/items.cpp:100`, `tools/hotbar.cpp:428`
- `src/editor/operations/operations_window.cpp:261,551,1063`
- `src/example/example.cpp:149,177,438-440`
- `src/rendering_test/*` (rendering_test, cell_stencil, cell_scene, cell_stencil_wide_line)

Reused infrastructure (no new abstractions):
- `erhe::buffer::Free_list_allocator` -- backs every pool block.
- `erhe::buffer::Buffer_allocation` -- RAII handle on `Buffer_mesh` is
  unchanged; lifetime guarantee that pool > Buffer_mesh holds because
  `Mesh_memory` is app-scoped.
- `erhe::graphics::Lazy_render_pipeline` -- shadow_renderer's existing
  pipeline cache pattern (`shadow_renderer.cpp:87-121`) applies per format.

## Risks and validation notes

- **Cpu_buffer_sink path.** `Buffer_range::buffer = nullptr` for ranges
  produced by the CPU sink. Audit consumers of CPU-backed ranges
  (`Primitive_raytrace`, gltf importer) before Step 1 to confirm none
  dereference the new field. Both paths today use `Cpu_buffer` shared_ptrs,
  so they should be unaffected.
- **Pre-baked `Vertex_input_state` in pipelines.** Today
  `app_rendering.cpp` builds every `Render_pipeline_state` against
  `&mesh_memory.vertex_input` (the default format). Step 6 requires every
  pipeline that can render skinned meshes to also exist for the skinned
  vertex_input -- and the bucket loop picks the right one per bucket.
- **Quest / OpenGL.** Quest uses real `glMultiDrawElementsIndirect`. The
  bucket pass introduces N draw calls instead of 1 per mesh-span; with
  current single-format scenes N=1 and zero overhead. With two formats N=2
  -- still trivial. No new validation surface.
- **Vulkan emulated MDI** (`vulkan_render_command_encoder.cpp:828-846`).
  Per-bucket binds are compatible; the per-draw push constant remains
  unchanged within a bucket.
- **Pipeline cache size.** `shadow_renderer.cpp:82` resizes the cache to 8
  entries. Multi-format may need a larger cap; revisit at Step 6.
- **Thread safety.** Pool `allocate` takes a per-pool mutex around
  walk-and-grow. Mirrors the existing `Graphics_buffer_sink` mutex pattern.
- **Serialization.** `scene_serialization.cpp:730` reads
  `vertex_buffer_ranges`; confirm the new `Buffer*` field is excluded from
  serialized output (pointers don't serialize).

## Verification

End-to-end checks per step:

- **Step 0**: `scripts\configure_vs2026_opengl.bat` build clean; editor
  launches; a scene with one mesh renders unchanged. Confirm
  `Buffer_range::buffer` is populated by adding a temporary log line (revert
  before merge).
- **Step 1**: Same scene renders unchanged. Add a temporary mismatch in one
  primitive's stream to verify the assertion fires.
- **Step 2**: Behavior unchanged; pool-owning is a structural refactor.
  Build + scene render check is sufficient.
- **Step 3**: Editor launches with empty pools. Load a scene; pools grow on
  first allocation. VRAM measured before/after scene load shows the lazy
  growth (e.g. via Tracy memory plot or NSight).
- **Step 4**: Single-format scenes still render unchanged. Inject a
  synthetic two-bucket case (e.g. by force-allocating a second block in a
  pool) and verify two MDI calls are issued via Tracy capture or RenderDoc.
- **Step 5**: Register a second `Vertex_format` and a few meshes against
  it; confirm distinct pools are created and selected. Unit-test:
  `compute_vertex_format_key` returns the expected bit mask for each
  canonical format, and two `Vertex_format`s constructed differently but
  with the same attribute set produce the same key.
- **Step 6**: Static and skinned meshes coexist in one scene; both render
  with their correct vertex input. Verify on Windows OpenGL build first,
  then run on Quest via `scripts\install_android.bat quest run` (per
  `erhe-quest-launch` skill -- install first, then prompt user to don
  headset).
- **Step 7**: Wide-line debug rendering still works for both formats;
  verify on Quest where the OpenGL-ES compute path is exercised.

Build configurations to validate at each step (per `CLAUDE.md`; always use
the `scripts\` wrappers, never `cmake --preset` directly):
- Windows: `scripts\configure_vs2026_opengl.bat`, build `editor` target.
- Quest: `scripts\install_android.bat quest` then `... quest run` after
  user dons the headset.
- Optional Vulkan: `scripts\configure_vs2026_vulkan.bat` to exercise the
  multi-draw-emulated path.
