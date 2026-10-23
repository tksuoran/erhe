# Explicit resource barriers: chaining the application

Status: in progress

`Command_buffer::texture_barrier` / `buffer_barrier` exist on every backend
(`doc/erhe/graphics.md` "Resource barriers"). Backend-side hazard tracking is
rejected (the main thread's prepare phase and the render thread record at
the same time, so a backend cannot know the submission order); the
application states each dependency. This plan is commit B: chain every
dependency the editor and the libraries still leave to the backends, then
delete the Metal 4 backend's unconditional encoder-start barrier
(`encode_queue_barrier` inside `Command_buffer_impl::encode_start_barriers`)
together with the `ERHE_METAL4_EXPLICIT_BARRIERS_ONLY` test switch
(`doc/erhe/metal4_backend.md` "Encoders and barriers").

## Done when

- The editor's image gates (`doc/editor/render_thread.md` "Metal") are
  byte-identical on Metal 4 with `ERHE_METAL4_EXPLICIT_BARRIERS_ONLY=1`
  before the deletion, and the 300-frame soak is clean, with every threading
  configuration.
- Vulkan: no `texture_barrier ... a barrier chain is missing` warning in
  `logs/log.txt` over the gates, and the validation layer with synchronization
  validation is clean. The warning then becomes a VERIFY in
  `Texture_impl::record_usage_barrier`.
- Decided and done: the fate of `transition_texture_layout` and
  `cmd_texture_barrier` (expected: every caller becomes a `texture_barrier`,
  then both go), of the Vulkan record-time layout tracking (it goes when no
  backend-derived transition is left), and of the Metal 4 per-dispatch
  `encode_intra_encoder_barrier` (expected: replaced by explicit barriers
  between dependent dispatches).

## Render graph pins

Pass-to-pass texture dependencies belong to the render graph, which runs on
one recording thread per frame and knows producer and consumer of every
pin. A pin gains an access kind (an `Image_usage_flag_bit_mask` value with
stage qualifiers: `color_attachment`, `depth_stencil_attachment`,
`sampled | fragment_shader`, `storage | compute_shader`, ...); the graph emits
`texture_barrier(output texture, producer access, consumer access)` between
the producing node's execute and the consuming node's, and the nodes stop
declaring `user_synchronized` / ad hoc barriers for graph edges
(`src/erhe/rendergraph/erhe_rendergraph/rendergraph_node.hpp`
`register_input` / `register_output`).

## Hand-recorded chains

Dependencies inside one node or across the frame boundary, recorded by hand:

- GPU culling: `cull_classify` -> `cull_compact` -> the Metal ICB encoding
  kernel -> the GPU-counted draws
  (`src/erhe/scene_renderer/erhe_scene_renderer/draw_list_scene_gpu_cull.cpp`,
  `gpu_cull_classifier.cpp`, `src/erhe/graphics/erhe_graphics/indirect_count_draws.cpp`):
  `buffer_barrier(storage_write | compute_shader -> storage_read | compute_shader)`
  between the kernels, `-> indirect` before the draws.
- Light cluster binner -> forward fragment reads
  (`light_cluster_gpu_binner.cpp` -> `forward_renderer.cpp`):
  `buffer_barrier(clusters, storage_write | compute_shader -> storage_read | fragment_shader)`.
- Shadow passes -> forward sampling (`src/editor/rendergraph/shadow_render_node.cpp`):
  a graph edge, `depth_stencil_attachment -> sampled | fragment_shader`
  (plus `vertex_shader` while the shadow debug visualization samples in the
  vertex stage).
- Main viewport -> post-processing chain (`src/editor/rendergraph/post_processing.cpp`):
  graph edges plus the pyramid level-to-level chains inside the node
  (`texture_barrier` with a level `range`).
- Viewport texture -> swapchain ImGui pass
  (`src/erhe/imgui/erhe_imgui/imgui_renderer.cpp`, `window_imgui_host.cpp`),
  and the cross-frame write-after-read: frame N+1's first pass writing a
  texture frame N's ImGui pass sampled
  (`sampled | fragment_shader -> color_attachment`, with `discard` added to
  `usage_before` when the pass overwrites every texel; scope
  `earlier_submissions` when the frames record on different threads). The
  swapchain image's first pass of a frame starts from its acquire:
  `present | discard -> color_attachment`. `none` never stands for "the
  previous frame's contents are not needed": it means never accessed, and
  orders nothing.
- Texture uploads and mipmaps -> sampled (`Command_buffer::upload_to_texture`
  callers, `src/erhe/gltf/erhe_gltf/image_transfer.cpp`,
  `Blit_command_encoder::generate_mipmaps` callers in `src/editor/`):
  `transfer_dst -> sampled`; uploads recorded by the main role and sampled by
  the render role use `earlier_submissions`.
- Acceleration structure builds (`src/editor/renderers/scene_tlas.cpp`,
  `lightmap_baker.cpp`, `Acceleration_structure::record_build`):
  build input `transfer_dst | host_write -> acceleration_structure_build_input`,
  bottom -> top level `acceleration_structure_write -> acceleration_structure_read`,
  top level -> ray queries `acceleration_structure_write -> acceleration_structure_read | compute_shader`.
- Readbacks: id pass (`src/editor/renderers/id_renderer.cpp`), scene image
  capture (`src/editor/scene/scene_image_capture.cpp`), GPU cull count and
  check readbacks, DDGI, radiance cascades and ray trace readbacks
  (`ddgi_renderer.cpp`, `radiance_cascades_renderer.cpp`,
  `ray_trace_renderer.cpp`): `color_attachment | storage -> transfer_src`
  before the copy, `transfer_dst -> host_read` after it.
- Lightmap baker (`src/editor/renderers/lightmap_baker.cpp`): G-buffer
  raster -> compute, the per-tile ping-pong, atlas -> forward sampling.
- Backend-internal encoders that act on application resources (Metal 4
  `clear_texture`, `upload_to_texture`, blit encoder copies and mipmaps, the
  drawable capture in `Swapchain_impl::record_capture`): their callers chain
  them like any other transfer.

## `memory_barrier()` call sites

The 39 sites outside `erhe_graphics` and the tests
(`grep -rn "memory_barrier(" src --include=*.cpp`): 37 calls and two
comments that name the caller's barrier. Each call becomes the named
per-resource barrier; none needs the global form.

| Site | Today | Convert to |
|---|---|---|
| `scene_renderer/light_cluster_gpu_binner.cpp:307` | storage + pixel buffer | `buffer_barrier(clusters, storage_write \| compute_shader, storage_read \| fragment_shader)` + `(counters, storage_write \| compute_shader, transfer_src)` |
| `scene_renderer/draw_list_scene_gpu_cull.cpp:330` | pixel buffer | `buffer_barrier(mask buffer, storage_write \| compute_shader, transfer_src)` |
| `scene_renderer/draw_list_scene_gpu_cull.cpp:714` | storage | `buffer_barrier(mask buffer, storage_write \| compute_shader, storage_read \| compute_shader)` |
| `scene_renderer/draw_list_scene_gpu_cull.cpp:734` | storage | `buffer_barrier(pass bits, storage_write \| compute_shader, storage_read \| compute_shader)` |
| `scene_renderer/draw_list_scene_gpu_cull.cpp:749` | command + storage + pixel buffer | `buffer_barrier(command buffer, storage_write \| compute_shader, indirect \| storage_read)` + `(count buffer, storage_write \| compute_shader, indirect \| transfer_src)` |
| `editor/renderers/ddgi_renderer.cpp:1134` | client mapped | `buffer_barrier(reference output, storage_write \| compute_shader, host_read)` |
| `editor/renderers/ddgi_renderer.cpp:1288` | client mapped | `buffer_barrier(query output, storage_write \| compute_shader, host_read)` |
| `editor/renderers/ddgi_renderer.cpp:1617` | client mapped | `buffer_barrier(probe readback, transfer_dst, host_read)` |
| `editor/renderers/ddgi_renderer.cpp:1993` | image access + 2 transitions | `texture_barrier(ray data, storage \| compute_shader, sampled \| compute_shader)`; irradiance / distance `sampled -> storage \| compute_shader` (the blend reads the previous values, which earlier frames sampled; no `discard`) |
| `editor/renderers/ddgi_renderer.cpp:2032` | image access + 4 transitions | `texture_barrier(each atlas, storage \| compute_shader, sampled)` |
| `editor/renderers/sky_renderer.cpp:392` | image access | `texture_barrier(transmittance, storage \| compute_shader, storage \| compute_shader)` (`sampled \| compute_shader` on the no-storage-read workaround path) |
| `editor/renderers/sky_renderer.cpp:417` | image access + 2 transitions | `texture_barrier(each LUT, storage \| compute_shader, sampled \| fragment_shader)` |
| `editor/renderers/lightmap_baker.cpp:4821` | image access + 3 transitions | `texture_barrier(each G-buffer texture, storage \| compute_shader, sampled)` |
| `editor/renderers/lightmap_baker.cpp:5074` | image access + transition | `texture_barrier(working, sampled, storage \| compute_shader)` |
| `editor/renderers/lightmap_baker.cpp:5088` | image access | `texture_barrier(working, storage \| compute_shader, storage \| compute_shader)` |
| `editor/renderers/lightmap_baker.cpp:5108` | image access + 2 transitions | `texture_barrier(normal / albedo, storage \| compute_shader, sampled \| compute_shader)` |
| `editor/renderers/lightmap_baker.cpp:5140` | image access (ping-pong) | `texture_barrier(ping[i + 1], storage \| compute_shader, storage \| compute_shader)` |
| `editor/renderers/ray_trace_renderer.cpp:425` | client mapped + transition | `texture_barrier(output, transfer_src, sampled)` + `buffer_barrier(readback, transfer_dst, host_read)` |
| `editor/renderers/ray_trace_renderer.cpp:598` | image access + transition | `texture_barrier(output, storage \| compute_shader, sampled \| fragment_shader)` |
| `editor/renderers/id_renderer.cpp:746` | storage | `buffer_barrier(bitmask / output, transfer_dst, storage_read \| storage_write \| compute_shader)` |
| `editor/renderers/id_renderer.cpp:762` | storage | `buffer_barrier(bitmask, storage_write \| compute_shader, storage_read \| compute_shader)` |
| `editor/renderers/id_renderer.cpp:777` | pixel buffer | `buffer_barrier(output, storage_write \| compute_shader, transfer_src)` |
| `editor/renderers/radiance_cascades_renderer.cpp:1635` | image access + transitions | `texture_barrier(each state, storage \| compute_shader, sampled \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:1698` | image access between dispatches | `texture_barrier(cascade raw, storage \| compute_shader, storage \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:1796` | image access + transitions | `texture_barrier(each raw atlas, storage \| compute_shader, sampled \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:1834` | image access between dispatches | `texture_barrier(neighbour atlas, storage \| compute_shader, storage \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:1905` | image access + transitions | `texture_barrier(each neighbour atlas, storage \| compute_shader, sampled \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:2020` | image access + transition | `texture_barrier(merged, storage \| compute_shader, sampled \| compute_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:2096` | image access + 3 transitions | `texture_barrier(each field atlas, storage \| compute_shader, sampled)` |
| `editor/renderers/radiance_cascades_renderer.cpp:2192` | image access + 3 transitions | `texture_barrier(each, storage \| compute_shader, sampled \| fragment_shader)` |
| `editor/renderers/radiance_cascades_renderer.cpp:2735` | client mapped | `buffer_barrier(each readback, transfer_dst, host_read)` |
| `editor/xr/headset_view.cpp:1239` | vertex attrib + storage | `buffer_barrier(debug line triangles, storage_write \| compute_shader, storage_read \| vertex_shader)` (plus `vertex` for a vertex-input consumer) |
| `editor/xr/headset_view.cpp:1480` | vertex attrib + storage | as `headset_view.cpp:1239` |
| `editor/scene/viewport_scene_view.cpp:572` | vertex attrib + storage | as `headset_view.cpp:1239` (debug renderer triangles) |
| `editor/scene/viewport_scene_view.cpp:638` | vertex attrib + storage | as `headset_view.cpp:1239` (content wide line triangles) |
| `editor/scene/viewport_scene_view.cpp:658` | texture fetch | `texture_barrier(shadow map, depth_stencil_attachment, sampled \| vertex_shader \| fragment_shader)` (a graph edge once pins carry access kinds) |
| `editor/scene/scene_image_capture.cpp:476` | client mapped | `buffer_barrier(capture buffer, transfer_dst, host_read)` |
| `renderer/debug_renderer.cpp:628` | comment: the caller's compute -> vertex barrier (`viewport_scene_view.cpp:572`, `headset_view.cpp:1239`) | rewrite the comment to name the caller's `buffer_barrier` |
| `scene_renderer/content_wide_line_compute_renderer.cpp:368` | comment: the caller's compute -> vertex barrier (`viewport_scene_view.cpp:638`, `headset_view.cpp:1480`) | rewrite the comment to name the caller's `buffer_barrier` |

## Order of work

1. Render graph pin access kinds and the graph-emitted barriers.
2. The hand-recorded chains and the table above, one subsystem per commit,
   each verified on Metal 4 with `ERHE_METAL4_EXPLICIT_BARRIERS_ONLY=1` (its
   gate images) and on Vulkan (no chain warning).
3. The deletion: the unconditional barrier, the test switch, then the
   leftovers named under "Done when".
