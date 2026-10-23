# Resource barriers: the declaration baseline and its narrowing

Status: in progress

The application states its GPU dependencies; no backend infers them
(backend-side hazard tracking is rejected: the main thread's prepare phase
and the render thread record at the same time, so a backend cannot know the
submission order). The baseline every backend honours is what the
application already declares, the declarations Vulkan is correct with:
render pass attachment `usage_before` / `usage_after`,
`memory_barrier(mask)` (source: every stage), `transition_texture_layout` /
`cmd_texture_barrier`, and the transfers the backends record themselves
(uploads, clears, blits, mipmaps), which order their output for its
consumers (`doc/erhe/graphics.md` "Resource barriers").
`Command_buffer::texture_barrier` / `buffer_barrier` narrow it to one
resource and the two accesses of one dependency.

## Step 1: Metal 4 honours the baseline (done)

The Metal 4 backend maps every baseline declaration to a stage barrier
(`doc/erhe/metal4_backend.md` "Encoders and barriers"); the unconditional
barrier at every encoder start (`encode_queue_barrier`) and the
`ERHE_METAL4_EXPLICIT_BARRIERS_ONLY` test switch are gone. Producer
barriers (`barrierAfterStages`) order the later encoders and commits, as a
Vulkan barrier orders everything after it; a barrier still pending when a
command buffer ends is encoded as a producer barrier in an empty encoder.
The per-dispatch intra-encoder barrier stays (step 4).

Verification: the editor image gates (`doc/editor/render_thread.md`
"Metal", all four threading configurations) byte-identical to the build
with the unconditional barrier, twice; the Bistro image; the GPU culling
check (0 mismatches, CPU and GPU classification images equal); the ray
trace and radiance cascades images byte-identical and DDGI numerically
equal; Metal API validation, the TSan race gate and the 300-frame windowed
soak clean; `ctest -L "gpu|editor"` with the `Resource_barrier_test` cases.

## Step 2: narrowing where measured

Each broad declaration whose cost a measurement shows (Tracy GPU zones,
the barrier count per frame) becomes the per-resource barrier it stands
for: the `memory_barrier()` sites below (each orders every stage before
its consumers); the render pass end barriers whose unqualified `sampled`
`usage_after` also orders compute consumers; the producer barriers whose
consumers are known to be in the next encoder only, by a consumer barrier
there.

## Step 3: Vulkan tracked layouts

The `texture_barrier ... a barrier chain is missing` warning becomes a
VERIFY in `Texture_impl::record_usage_barrier` once the gates are silent.
Decided then: the fate of `transition_texture_layout` and
`cmd_texture_barrier` (expected: every caller becomes a `texture_barrier`,
then both go) and of the Vulkan record-time layout tracking (it goes when
no backend-derived transition is left).

## Step 4: the intra-encoder dispatch barrier

Metal 4 orders every dispatch after the first in a compute encoder after
the encoder's earlier dispatches (`encode_intra_encoder_barrier`), more
than Vulkan needs. It goes once the dependent dispatches inside one encoder
carry their barriers (`texture_barrier` / `buffer_barrier` or
`memory_barrier` between the dispatches), each verified with the
`Resource_barrier_test` in-encoder cases and the gates.

## Narrowing candidates: `memory_barrier()` call sites

The 39 sites outside `erhe_graphics` and the tests
(`grep -rn "memory_barrier(" src --include=*.cpp`): 37 calls and two
comments that name the caller's barrier. Each call can become the named
per-resource barrier; none needs the global form. They are candidates for
step 2, converted where a measurement shows the broad form costs something.

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
