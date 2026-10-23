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

### Measurement (2026-10-24, Apple M4 Pro)

Method. Release headless Metal 3 and Metal 4 trees (Tracy on, emulated
swapchain, so no drawable wait), frame pipelining on,
`force_disable_vsync` and `frame_pacing_tier=off` (the GPU saturates);
each run in an isolated working directory as
`scripts/render_thread_bench.py` makes one. A Metal System Trace
(`xcrun xctrace record --template 'Metal System Trace' --attach <pid>
--time-limit 2500ms`) is taken after an 8 s settle. Headless, the
`metal-gpu-intervals` rows carry no process, label or Instruments frame
number: they are joined through their `gpu-submission-id` to
`metal-gpu-submission-to-command-buffer-id` (pid, command buffer id,
encoder id). Encoders are ordered by encoder id; a frame is a run of
encoders on one command buffer object (the frame command buffers rotate);
200 frames from the middle of the trace. GPU idle is the time no channel
(Vertex, Fragment, Compute) is busy, inside a frame and from the last
interval of frame N to the first of N+1. The encoder names and the
emitted stage pairs come from a temporary Metal 4 log of every consumer,
producer and intra-encoder barrier, every `endEncoding` (with the
encoder label) and every commit, matched to the trace by ordinal in the
frame. Scenes: default, `heavy`, Bistro (the bench camera of
`scripts/bistro_frame_bench.py`) at the headless window size, and `draws`
at window 960x540, viewport 320x200.

Barriers per frame on Metal 4: 25 consumer + 25 producer (default,
heavy), 23 + 23 (Bistro), 21 + 21 (draws), 29 + 29 (heavy with GPU culling
classification and clustered lights), 32 + 33 with DDGI, 39 + 40 with
radiance cascades. Each post-processing pass has `V|F|D -> F` at begin and
`F -> V|F|D` at end; the main pass `all -> V|F` (pending
`memory_barrier`s) at begin.

GPU idle per frame (us; Metal 3 / Metal 4):

| Scene | GPU busy ms | idle inside frame | idle between frames |
|---|---|---|---|
| default | 1.436 / 1.436 | 13.2 / 11.8 (0.9 / 0.8 %) | 2.9 / 2.3 |
| heavy | 3.410 / 3.450 | 13.6 / 11.8 (0.4 / 0.3 %) | 36.9 / 37.8 |
| Bistro | 4.48 / 4.61 | 12.3 / 11.2 (0.3 / 0.2 %) | 5.7 / 5.1 |
| draws, small viewport | 0.740 / 0.822 | 10.3 / 9.3 (1.4 / 1.1 %) | 42.9 / 40.3 |

The gaps are the same on both back ends, encoder pair by encoder pair, so
none is the cost of a Metal 4 barrier: viewport overlay pass -> ImGui host
pass 3.1-3.8 us (a `F -> F` consumer barrier on Metal 4), each
post-processing pass -> the next 0.22-0.26 us (20 pairs), the in-scene
render target pass -> the capture blit 0.4-1.2 us. The gap before the main
pass (heavy 37 us, draws 40 us, default 2 us, Bistro 5 us) is equal on
Metal 3. Metal 4's main pass of `draws` is 75-85 us longer than Metal 3's
(1500 draws; no barrier inside the pass, and narrowing does not change
it).

What the broad barriers cost is overlap, not idle: on Metal 4 the
producer barrier `F -> V|F|D` at the end of each post-processing pass
holds the vertex stage of every later pass until the pass's fragment work
is done, so the per-pass vertex / tiling phase of the 20 small passes runs
serially. Experiment (measurement only, not committed): the unqualified
`sampled` `usage_before` / `usage_after` of the frame's sampled color
targets narrowed to `sampled | fragment_shader`
(`post_processing.cpp:268-269` and `:317-318`, `render_target.cpp:178`,
`:184`, `:186`, `viewport_scene_view.cpp:811`, `:817`, `:819`,
`rendertarget_mesh.cpp:151`, `:153`), which makes those passes `F -> F` at
both ends. GPU span after the main pass, Metal 3 / Metal 4, before ->
after (us; Bistro over three runs each, spread 1 us; its main pass varies
150 us run to run):

| Scene | Metal 3 | Metal 4 |
|---|---|---|
| default | 720 -> 722 | 717 -> 629 |
| heavy | 764 -> 763 | 757 -> 660 |
| Bistro | 707 -> 706 | 710 -> 615 |
| draws, small viewport | 231 -> 230 | 229 -> 158 |

The frame GPU span drops by 72-97 us on Metal 4 (default 6.1 %, heavy
2.7 %, Bistro 2.1 %, draws 8.7 %); idle inside the frame drops from 11-12
to 2-3 us; Metal 3 is unchanged (its encoders are fenced regardless). In
the bench (small viewport, pipelined, unpaced, three runs; GPU-bound in
Release on this GPU: `slot_acquire` is most of the tick):

| Scene, back end | tick before -> after | R | Tracy `Post_processing` GPU zone |
|---|---|---|---|
| draws, Metal 3 | 800 -> 802 | 374 -> 375 | 121 -> 121 |
| draws, Metal 4 | 876 -> 801 | 397 -> 396 | 120 -> 77 |
| default, Metal 3 | 286 -> 286 | 188 -> 188 | 119 -> (no capture) |
| default, Metal 4 | 281 -> 235 | 151 -> 149 | 116 -> 79 |

CPU-bound (Debug headless, `draws`, the same viewport, two runs): tick
6566 / 6615 us (Metal 3 / Metal 4) before, 6679 / 6783 after, R equal to
the tick and `slot_acquire` 0.2 us: no effect beyond the session's drift,
as expected of a GPU-side change.

The intra-encoder barrier (step 4) is encoded 0 times per frame in every
configuration measured (heavy with GPU culling classification, GPU light
binning, DDGI or radiance cascades): each dispatch records its own compute
encoder. The gaps between the consecutive compute encoders of the culling
chain are 0.3-0.6 us on both back ends; the `memory_barrier`s between them
(`all -> D`) cost nothing measurable.

What step 2 commits, in order of gain:

1. The sampled color targets of the frame's render passes declare their
   fragment-only use: `usage_before` / `usage_after` `sampled ->
   sampled | fragment_shader` at the sites of the experiment, each after
   checking that no compute or vertex consumer reads the target (the
   experiment did not check consumers or run the image gates). Expected:
   70-100 us GPU per frame on Metal 4 at any resolution (the cost is per
   pass, not per pixel), up to 17 % of the tick in the small-viewport
   bench. No change on Metal 3; Vulkan not measured.
2. The `memory_barrier()` sites below, and the main pass begin barrier they
   feed (`all -> V|F`): no measurable cost (the gap before the main pass is
   the same on Metal 3); converted as cleanup only.
3. The compute chain barriers (`draw_list_scene_gpu_cull.cpp`,
   `light_cluster_gpu_binner.cpp`): no measurable cost.

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
