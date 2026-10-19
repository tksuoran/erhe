# Editor render thread

Stability: mostly stable

The editor renders each frame on a dedicated render thread while the main
thread runs window input, the simulation, the OpenXR frame wait and the
editor windows, and prepares the next frame. This document owns the
requirements, the design and the map of the implementation; the order of the
steps inside each phase is in `doc/editor/editor.md` "Frame phases and the
render thread", and the device-level rules in `doc/erhe/graphics.md` "Frame
lifecycle" and "Thread safety".

## 1. Requirements

- **R1 (overlap).** While the render thread records and submits frame N, the
  main thread runs `xrWaitFrame` for frame N+1, polls window events and
  simulates physics.
- **R2 (no added input latency).** Input polled during frame N's render is
  consumed by frame N+1's preparation and shown by frame N+1's render: the
  same distance input travels on one thread (poll, prepare, render).
- **R3 (the main thread does not wait for the render thread).** The main
  thread prepares frame N+1 while the render thread renders frame N and
  blocks on the render thread only when it hands over frame N+1 and frame N
  is still rendering. The frame time is the longer of the main thread's own
  work and the render phase, plus one handoff, instead of their sum.
- **R4 (inline mode).** A single-thread mode in which the frame phases run
  back to back on the main thread stays available at run time: the mode of
  the OpenGL and null backends, the mode for bisecting a threading bug, and
  the mode the MCP and headless scripts can pin.
- **R5 (identical output).** Every mode and schedule renders the same
  image: a scene capture (`render_scene_image`) of the same scene is
  byte-identical across inline, dedicated serial, ImGui overlap and
  pipelined runs. This is the gate every change to the frame phases passes.
- **R6 (steady-state allocation).** The handoff allocates nothing per frame
  (AGENTS.md "Run-time Memory Allocation Discipline"): frame data crosses
  threads in a fixed-size packet owned by the editor, and the per-frame
  copies the render phase reads keep their capacity.
- **R7 (plain synchronization).** Mutexes and condition variables
  (AGENTS.md: no lock-free or atomic techniques without approval; the two
  `std::atomic<bool>` flags the main loop already used, the resize request
  and `m_in_tick`, stay).
- **R8 (backends).** Vulkan runs the dedicated thread and the pipelined
  schedule. Metal records from any thread and takes the same defaults.
  OpenGL needs its context on the recording thread and runs inline (section
  4).
- **R9 (enforcement).** Thread ownership and the frame phase are asserted
  in debug builds at the entry points that matter (section 2.7), the same
  way `Draw_list_scene` asserts its owner thread.

## 2. Design

### 2.1 Thread roles and frame phases

A frame is four phases. Three run on the main thread; the render phase runs
on the render role, which is the render thread in dedicated mode and the
main thread in inline mode. `erhe::utility::Frame_phase`
(`doc/erhe/utility.md`) names them; the functions are `Editor`'s.

| Phase | Thread | Function | Content |
|---|---|---|---|
| M1 overlap | main | `frame_overlap_phase()` | The device frame start and frame pacing, the simulation clock, `xrWaitFrame`, the physics simulation fixed steps. Writes nothing the render phase reads; what the render phase needs from it travels in the `Frame_packet`. |
| M2 prepare | main | `frame_prepare_phase()` | Everything else before the rendergraph: input dispatch, the device frame slot, uploads, animations, OpenXR events and actions, the node-writing fixed steps and physics write-back, commands, MCP, hover, operations, graph evaluation, message bus, mesh memory, transforms, draw lists, material sets, lightmap plan, and the per-frame copies the render phase reads (section 2.6). Ends by filling the packet and kicking the render phase. |
| M3 ImGui | main | `frame_imgui_phase()` | The editor windows of every ImGui host and the settings autosave. Reads live editor state and the M2 copies; records no GPU work; its draw data is copied per frame and published to the render phase. |
| R render | render role | `frame_render_phase()` | `xrBeginFrame`, present-wait clamp, swapchain wait and acquire, the upload submit with its GPU edge, the indirect diffuse ticks, the lightmap record, `Rendergraph::execute` (viewports, shadow nodes, ImGui hosts, headset node with its own submits and `xrEndFrame`), frame submit, `Device::end_frame`. |

Two schedules decide where the main thread joins the render phase of the
previous frame (`threading.frame_pipelining`, section 3.2).

Pipelined, the default with a dedicated render thread: M1, M2 and M3 of
frame N+1 run while R renders frame N; the kick of N+1 joins N first. At
most one frame is rendering and one is being prepared.

```
main:   | M1(N+1) | M2(N+1) | kick: join R(N) | M3(N+1) | publish | M1(N+2) | M2(N+2) | kick: join R(N+1) |
render: | R(N): scene ... ImGui hosts, submit |         R(N+1): scene ... wait | ImGui hosts, submit |  R(N+2)
```

Serial (pipelining off): M1 of the next frame still overlaps R, and the
main thread joins R(N) at the end of M1(N+1), before M2(N+1), so M2 and R
never overlap.

```
main:   | M2(N) | kick | M3(N) | publish | start(N+1), poll_events, M1(N+1) | join R(N) | M2(N+1) | kick |
render: |              R(N): scene ...        | wait | ImGui hosts, submit |   idle   | R(N+1)
```

The ImGui overlap (`threading.imgui_overlap`, on by default with a
dedicated render thread) runs M3 after the kick, so the editor windows of
frame N draw while R renders frame N's scene; R's ImGui host nodes, at the
end of the rendergraph, wait for the draw data M3 publishes. With the
overlap off, M3 runs before the kick. In inline mode every phase runs on
the main thread in the order M1, M2, M3, R.

Consequence of the overlap, accepted by design: a scene edit made in an
editor window during frame N is picked up by M2 of frame N+1 and rendered
by that frame; input dispatch, commands, hover and the tools run in M2
before the kick, so viewport interaction keeps R2.

### 2.2 OpenXR mapping

The OpenXR frame functions split by thread the way the specification
intends: `xrWaitFrame` is the throttle the simulation thread blocks on, and
it returns for frame N+1 once `xrBeginFrame` of frame N has been called, so
it runs in M1 while the render thread is between `xrBeginFrame(N)` and
`xrEndFrame(N)`. `xrBeginFrame` and `xrEndFrame` stay on one thread, the
render role, and `xrBeginFrame(N+1)` follows `xrEndFrame(N)` because R runs
frames in sequence.

- `Headset::wait_frame() -> Frame_timing` (M1) and
  `Headset::begin_frame(const Frame_timing&)` (R) are the two halves;
  `erhe::xr::Frame_timing` (`erhe_xr/frame_timing.hpp`) travels in the
  packet. A frame whose wait failed stores the failed timing and does not
  begin, so `xrBeginFrame` is never left unpaired.
- `xrPollEvent` is M2: the session-state transitions it applies are read by
  R's frame functions, and an `xrEndSession` landing between R's
  `xrBeginFrame` and `xrEndFrame` would break the frame bracket.
  `xrSyncActions`, hand tracking and the view pose (`update_actions`) are
  M2 too, since they write controller nodes and pointer state.
- `Xr_session::render_frame*()` and `end_frame()` run in R. The XR view
  command buffers come from the render thread's slot
  (`Xr_session::set_command_buffer_thread_slot`), and the frame command
  buffer is the one `render_frame*()` fans its view command buffers out
  from, so the upload edge of section 2.3 covers them.
- A frame whose `shouldRender` is false still gets its `xrBeginFrame` and
  `xrEndFrame` from R. When `poll_events` or `wait_frame` fails, the 250 ms
  throttle sleep runs on the main thread.
- The headset path (`Headset_view::render_headset`) prepares and renders
  its views inside R from live state: it records its tools against eye
  poses located in R, captures its joint poses after its own transform
  update, and renders its passes through `render_immediate` with the live
  material set slot table and pass predicates. Because of that, frame
  pipelining is forced off under OpenXR (the serial schedule keeps M2 and R
  exclusive) and the headset keeps its own `item_host_mutex` discipline.
  Moving the headset to the prepare-then-render model is future work
  (section 4).

### 2.3 Device frames and command buffers

The device (`doc/erhe/graphics.md` "Frame lifecycle") allows up to
frames-in-flight frames open at once, acquired and ended in id order; frame
F uses slot `F % N`. The editor's frame bracket spans both roles:

- `Device::wait_for_frame_start()` (M1) is the pacing half of the frame
  start, in the position of `xrWaitFrame`: it returns the id of the frame
  it starts from a counter only the main thread advances, opens that
  frame's time record and refreshes the GPU clock calibration. The frame
  pacer's decision for this frame (FR4 display advance, FR3 target and
  holdback, FR5 present-wait id; `doc/frame_pacing/requirements.md` "Frame
  phases") is made right after it and travels in the packet, so the display
  advance feeds the `Time::prepare_update` of the frame it was computed for.
- `Device::acquire_frame_slot(frame_id)` (M2) is the resource half: the
  timeline wait for frame F-N and the command pool resets of the slot. With
  pipelining R(N) may still record frame N in the other slot; the slot's
  previous user, F-N, was joined by the previous kick, so the slot is idle.
- `Device::end_frame(frame_id)` (R) ends the packet's frame after its last
  submit and signals the device timeline.
- `Device::get_frame_index()` is keyed by role: the dedicated render thread
  gets the frame it renders, every other caller the frame being prepared.
  Ring-buffer entries, completion handlers, deferred destruction and
  command buffers are stamped with the calling role's frame, so each role
  touches only its own frame's state and the forty callers outside the
  backends keep one code path.
- Command buffers carry the frame id they were allocated for;
  `get_command_buffer` verifies the role's slot holds that frame. The main
  thread records M2 into its own upload command buffer (thread slot 1,
  `App_context::current_command_buffer` during M2 only; null in M1, M3 and
  between frames), the render thread records the frame command buffer and
  the XR view command buffers from thread slot 2, so the two roles never
  share a per-thread command pool.
- The render phase submits the upload command buffer first, device-only,
  with `upload_cb.signal_gpu_completion_to(frame_cb)` and
  `frame_cb.wait_for_gpu_completion_signal()`, so the frame's GPU work
  starts after the uploads completed.
- A standalone main-thread submit (record, submit, `wait_idle`) that reads
  or writes resources the render phase records with calls
  `App_context::join_render_phase` first: with pipelining that joins the
  outstanding frame as the next kick would; in the serial schedule it is a
  no-op. The lightmap baker and streamer use it.
- Every device entry point both roles reach is guarded by a mutex held only
  for bookkeeping (the queue mutex is the one held across a Vulkan call), a
  split per frame-in-flight slot, or one owning role; the table is
  `doc/erhe/graphics.md` "Thread safety". Completion handlers fire in
  `end_frame` on R and in `wait_idle`.

### 2.4 Swapchain and window

The desktop swapchain belongs to R: `VkSwapchainKHR` is host-synchronized
in `vkWaitForPresentKHR`, `vkAcquireNextImageKHR` and `vkQueuePresentKHR`,
and `Swapchain_impl` has one state machine and one acquired image. The FR5
present-wait clamp (with the packet's present-wait id),
`Command_buffer::wait_for_swapchain`, `begin_swapchain` and the presenting
submit all run in R; M2 touches neither the swapchain nor the frame command
buffer. The window ImGui host's swapchain render pass is sized in R from the
window size the packet carries.

`poll_events` stays on the main thread (SDL requires it). Window size and
the resize request cross through the packet (`Frame_begin_info`,
`window_width` / `window_height`); `Frame_activity` is classified by `run()`
and decides `should_render`: a hidden frame runs no acquire, rendergraph or
present and still submits the uploads device-only. A failed
`wait_for_swapchain` (an Android surface between background and foreground)
makes the frame a non-rendered one and reports `Frame_result::no_swapchain`;
the main thread sleeps 50 ms after the join that reports it. The live-resize
redraw callback calls `frame()` like `run()` does, guarded by `m_in_tick`.

### 2.5 The handoff

`Render_thread` (`src/editor/render_thread.hpp`), owned by `Editor` and
constructed after the parts it drives:

- `Frame_packet`: the per-frame inputs R reads, written by M1 (device frame
  id, the pacer's display advance, present target, holdback and
  present-wait id, frame number, host times, fixed-step dt, the OpenXR
  `Frame_timing`) and M2 (the upload command buffer, `should_render`, the
  `Frame_begin_info`, the window size, the lightmap atlas binding, the ray
  trace switch, the scene the indirect diffuse producers tick on). Fixed
  size; one instance owned by `Editor`, rewritten each frame; `kick()`
  copies it, so M1 of the next frame rewrites the editor's copy while R
  renders from `Render_thread`'s.
- `Frame_result`: what R reports back at the join: the device frame id it
  rendered, its durations (render, swapchain wait and acquire, draw data
  wait), the handoff latencies, the join wait's begin and end,
  `no_swapchain`, and the sky LUT readiness. The main thread adds the
  timings into its phase timing and publishes the frame id as
  `App_context::last_rendered_frame_index` (section 2.6).
- `kick(const Frame_packet&) -> optional<Frame_result>`: the main thread's
  handover at the end of M2. With pipelining it first joins the frame
  kicked by the previous `frame()` and returns its result; the serial
  schedule has joined already (`join()` at the end of M1) and gets nothing.
  In inline mode `kick()` runs R on the calling thread.
- `publish_draw_data(frame_number)` / `wait_for_draw_data(frame_number)`:
  the ImGui handoff. M3 publishes once every host's draw data is copied
  (`Imgui_windows::end_frame`); each ImGui host node in R waits, through
  `Imgui_renderer::set_draw_data_wait`, before it reads its frame's copy.
  The wait is slot-exact: it returns when that frame number itself was
  published and verifies that no later frame overwrote the entry. The wait
  runs before the host takes the ImGui renderer mutex, since M3 needs that
  mutex to draw (the opposite order deadlocked).
- One mutex and three condition variables (kick, done, draw data) carry the
  handoff; the packet and result are written only by the side that owns the
  current state. The dedicated thread publishes itself as the render role
  inside the constructor's handshake, before the constructor returns, and
  is named "Render thread" in Tracy. An exception in R is logged on
  `editor.frame` with the frame number and rethrown on the render thread.
- `stop()`: `~Editor` stops the watchdog, joins a frame still outstanding,
  ends the dedicated thread, and only then quiesces the GPU and destroys
  the parts.

### 2.6 Data ownership rules

These rules make M1, M2 and M3 free to edit live editor state while R
renders, with no lock on the render read side. Each is a standing rule for
new code.

- **R reads per-frame copies only.** Every value R reads that a main-thread
  phase writes reaches it as one of: a `Frame_packet` field; a slot per
  frame in flight, filled by M2 for the frame being prepared and read by R
  by the role-keyed `Device::get_frame_index()`, each slot stamped with its
  frame and verified at the read; or a value R itself owns. The slots are
  `Render_settings_frames` (the editor and graphics settings sections R
  reads, behind `App_context::render_settings`, a `Render_settings_access`
  that resolves to the calling role's copy), `Viewport_render_state` and
  `Scene_view::Render_view` per view (scene root, cameras as
  `erhe::scene::Camera_snapshot`, viewport, hover, pick probe, tools slot,
  grid, render target size), the scene root's `Scene_render_settings`,
  `Light_set` (every slot an `erhe::scene::Light_snapshot`) and
  `Joint_poses`, the draw list publishes, shadow bounds, bucket snapshots
  (`erhe::scene_renderer::Render_bucket_snapshot`), id pass, edge-line
  feed, ray trace scene, lightmap tick slot and composer slots, the debug
  and text renderer frames, the material set's published buffer copy and
  texture heap, the ImGui draw data copies, and the rendergraph's per-frame
  node list, enabled flags and `Prepared_frame_values<T>` (the M2 to R
  counterpart of `Render_frame_results`). A list or record set is published
  into a slot only when its change serial moved since that slot's last
  publish, so the steady state copies nothing.
- **The last M2 step reads the scene.** The composition prepare
  (`App_rendering::prepare_composition`: pass gating, bucketing into
  complete primitive records, the id pass, the edge-line feed, the shadow
  casters) runs after the material set updates and the lightmap plan, under
  each root's `item_host_mutex`, so the records carry this frame's material
  slots, lightmap uv transforms, transforms, flags and meshes. R applies
  only the environment shader-key axes it owns (light counts, shadow axes,
  multiview count, DDGI) and dereferences no `Mesh`, `Node`, `Primitive` or
  `Material`.
- **M2 decides, R applies.** Every resizable GPU object R uses (view render
  targets, post-processing pyramids, shadow maps, the BRDF slice and depth
  visualization targets) is created, replaced and dropped by R only, at the
  start of the owning node's execute, from a size or configuration M2
  requested for the frame (`Texture_rendergraph_node::request_size`,
  `Shadow_render_node::request_configuration`). The old object is released
  through `Rendergraph::defer_resource` at the end of the execute. A window
  that shows such a texture passes a reference R resolves (the node,
  `Render_texture_reference`) and sizes it from what R published last.
- **R writes for the main thread per frame in flight.**
  `Render_frame_results<T>` (`renderers/render_frame_results.hpp`): R writes
  the slot of the frame it renders (asserted on the render thread); the main
  thread reads the result of `App_context::last_rendered_frame_index`, the
  newest frame it joined, and code that runs in both phases uses
  `App_context::get_results_frame_index()`. A result for a frame in which
  the owner did not render is absent, never an older frame's. The results
  are the shadow node's `Shadow_frame_result` (self-contained light
  projections, draw statistics, CPU time), `Composition_pass_stats`, the
  DDGI and radiance cascades `Rendered_state` and the ray tracer's
  `Rendered_output`. With pipelining they reach the main thread one frame
  later than in the serial schedule.
- **ImGui-time work is deferred.** A window's `imgui()`, a menu entry and a
  host begin callback record no GPU work and mutate nothing R reads; they
  queue an operation for the next M2 (`Imgui_windows::queue`, flushed at
  the start of M2 with the upload command buffer open) or set a request the
  owner serves in its own M2 step (`doc/editor/coding_rules.md` "ImGui-time
  work"). `current_command_buffer` is null in M3 and
  `Thread_roles::assert_command_recording_allowed` refuses the main thread
  there, so a window that still records aborts instead of racing R.
- **Requests from the main thread into R go under a mutex** (the DDGI probe
  state request, the radiance cascades preview request, the MCP diagnostics
  that hand a request to R and read the answer once the frame retired on
  the GPU: `render_scene_image`, `set_ray_trace` `save_path`, the irradiance
  and reference queries, the radiance cascades texel copy; `doc/editor/editor.md`
  "MCP diagnostics that read R's output").
- **Rendergraph node lifetime.** The node list, enabled flags and pin links
  are copied per frame in `Rendergraph::prepare_frame`; `execute` runs its
  frame's slot without the graph mutex; a node released while an unrendered
  frame names it is retired until that frame renders
  (`doc/erhe/rendergraph.md` "Node lifetime"), and its release hook removes
  it at once from the registries the main phases iterate.
- **Physics step state** is read in R (the physics debug visualization, the
  drag point body display) under `Scene_root::get_physics_step_mutex()`,
  which M1 holds per step call; body activation flag edits the steps report
  are queued for M2.

### 2.7 Enforcement

`erhe::utility::Thread_roles` (`erhe_utility/thread_roles.hpp`,
`doc/erhe/utility.md`) holds the main thread id, the render thread id and
one `Frame_phase` per role, unsynchronized by design: the ids are set once
by the role thread before any frame, and each phase is written and read by
one thread (in inline mode the main thread owns both). Every assert is a
no-op while the role it compares against is unset, so the unit tests, the
GPU tests, `src/example`, `hello_swap`, `hextiles` and the editor's init pump
are outside the frame model.

- Render side (`assert_render_thread` + `assert_frame_phase(r_render)`):
  `Rendergraph::execute`, `Draw_list_scene::draw_color` / `draw_shadow` /
  `get_shadow_bounds`, `Imgui_renderer::render_draw_data`,
  `Headset_view::render_headset`, `Xr_session::begin_frame` /
  `render_frame*` / `end_frame`, `Lightmap_baker::record_tick`,
  `Render_frame_results::write`, the debug and text renderers' compute,
  render and release, `Device::begin_frame` / `end_frame` /
  `set_present_target_time`.
- Main side (`assert_main_thread_in_phase`: the main thread in the given
  phase or `none`): `Draw_list_scene::flush_pending` /
  `prepare_shadow_bounds`, `Imgui_windows::begin_frame`,
  `Operation_stack::update`, `App_message_bus::update`,
  `Scene_root::after_physics_simulation_steps` and the body activation
  paths, the lightmap partitioner, window, baker prepare and streamer
  updates (`m2_prepare`); `Xr_session::wait_frame` (`m1_overlap`);
  `Device::acquire_frame_slot` (main thread).
- `Device::get_command_buffer` calls `assert_command_recording_allowed`: the
  main thread obtains no command buffer in `m1_overlap` or `m3_imgui`.
- Per-frame slots verify at the read that the frame asked for is the frame
  that filled them; `Render_thread::wait_for_draw_data` verifies its entry.

## 3. Implementation

### 3.1 Code map

| Where | What |
|---|---|
| `src/editor/editor.cpp`: `run()`, `frame()`, `frame_overlap_phase()`, `frame_prepare_phase()`, `frame_imgui_phase()`, `frame_render_phase()`, `kick_frame()`, `on_frame_joined()` | The frame loop and the four phases; the thread slot constants; the mode, overlap and pipelining selection with their environment overrides; the phase timing log (3.3). Step order: `doc/editor/editor.md` "Frame phases and the render thread". |
| `src/editor/render_thread.hpp` / `.cpp` | `Frame_packet`, `Frame_result`, `Render_thread` (section 2.5). |
| `src/editor/app_context.hpp` | `render_thread_id`, `current_command_buffer`, `render_phase_host_system_time_ns`, `join_render_phase`, `last_rendered_frame_index`, `get_results_frame_index()`. |
| `src/editor/renderers/render_settings.hpp`, `render_settings_access.hpp`, `render_frame_results.hpp`, `prepared_frame_values.hpp`, `render_texture_reference.hpp` | The per-frame copies of section 2.6: settings (M2 to R), results (R to main), rendergraph node values (M2 to R), texture references the ImGui phase hands to R. |
| `src/editor/scene/viewport_render_state.hpp`, `Scene_view::Render_view`, `Scene_root::Render_snapshot_slot`, `Composer_frame_slots`, `Ray_trace_scene` | Per-view and per-scene slots (`doc/editor/scene.md`, `doc/editor/raytrace.md` "Ray_trace_scene"). |
| `src/erhe/utility/erhe_utility/thread_roles.hpp` | `Frame_phase`, `Thread_roles` (section 2.7; `doc/erhe/utility.md`). |
| `src/erhe/graphics`: `Device::wait_for_frame_start` / `acquire_frame_slot` / `end_frame(frame_id)` / `get_frame_index`, `Command_buffer::get_frame_id`, `Multi_copy_buffer::publish`, the Vulkan `Device_impl` mutexes and per-role ring buffer lists | The two-frames-open device bracket and its thread safety (`doc/erhe/graphics.md` "Frame lifecycle", "Thread safety"; `doc/erhe/ring_buffer_memory.md` "Roles and threads"; `doc/erhe/draw_list_material_set.md` D9). |
| `src/erhe/xr`: `Headset::wait_frame` / `begin_frame`, `Frame_timing`, `Xr_session::set_command_buffer_thread_slot`, the quad layer wait and release in `Xr_session` | The OpenXR split (section 2.2; `doc/erhe/xr.md`). |
| `src/erhe/scene`: `Camera_snapshot`, `Light_snapshot`; `src/erhe/scene_renderer`: `Light_set`, `Joint_poses`, `Draw_list_scene::publish` / `prepare_shadow_bounds`, `Render_bucket_snapshot`, `Content_wide_line_feed`, `Shadow_renderer::prepare` | The scene-side copies (`doc/erhe/scene.md`, `doc/erhe/scene_renderer.md`, `doc/erhe/draw_list_renderer.md`). |
| `src/erhe/renderer`: `Debug_renderer` / `Text_renderer` frames | Recording in M2 into a frame slot, drawing and releasing in R (`doc/erhe/renderer.md`). |
| `src/erhe/rendergraph`: `Rendergraph::prepare_frame`, `Frame_slot`, `make_rendergraph_node`, `Rendergraph_node_deleter`, `Texture_rendergraph_node::request_size` | The per-frame node list and node retirement (`doc/erhe/rendergraph.md`). |
| `src/erhe/imgui`: `Imgui_draw_data_copy`, `Imgui_texture_request`, `Imgui_renderer::set_draw_data_wait`, `Imgui_host::copy_draw_data` | The ImGui draw data copies and texture request queue (`doc/erhe/imgui.md` "Threading"). |
| `src/erhe/frame_pacing`: `Frame_time_recorder` | Thread-safe record setters; the `join_wait` and `present_wait` record pairs (`doc/frame_pacing/inputs.md` 3.2). |

### 3.2 Settings and overrides

`threading` in the editor settings, read at startup; each is logged once
with its source ("Render thread mode: ...", "ImGui overlap: ...", "Frame
pipelining: ...") and fixed for the session:

| Setting | Default | Override | Rule |
|---|---|---|---|
| `render_thread` (`Render_thread_mode`: `inline_main_thread`, `dedicated_thread`) | `dedicated_thread` | `ERHE_RENDER_THREAD=0|1` | The OpenGL and null backends run inline. |
| `imgui_overlap` | on | `ERHE_IMGUI_OVERLAP=0|1` | Off without a dedicated render thread. |
| `frame_pipelining` | on | `ERHE_FRAME_PIPELINING=0|1` | Off without a dedicated render thread and under an OpenXR headset (section 2.2). |

The `openxr_editor_settings.json` configuration runs the dedicated thread
with the serial schedule.

### 3.3 Phase timing and diagnostics

Every 60 frames `editor.frame` logs the per-frame averages of M1 (with
`frame_start_wait`), M2 (with `slot_acquire` and its six consecutive
sub-terms), M3, R, `swapchain`, `draw_data_wait`, `join_wait` (the main
thread blocked on the previous frame's R: in `join()` at the end of M1, or
inside the kick with pipelining; R minus `join_wait` is the overlapped part
of R), the kick and join handoff latencies, and the full tick. With
pipelining the R terms of a line belong to the frames joined in that
interval. The terms are defined in `doc/editor/editor.md` "Phase timing".

The main-loop stall watchdog keeps the main and render roles' phases apart
and names the thread and phase of a stall (`doc/agents/debugging.md`
"Main-loop stall watchdog"); the same document lists the thread ownership
assert messages. "Main loop: completed frame N" is logged at the end of R
for frames 1 to 12; run scripts wait for frame 12.

### 3.4 Verification

- **Image gate (R5).** `render_scene_image` captures of the default scene,
  a skinned import, a light edit, a bake, DDGI and radiance cascades
  scenes, wide-line and shadow cases, taken in the inline, dedicated,
  overlap and pipelined configurations (`ERHE_RENDER_THREAD`,
  `ERHE_IMGUI_OVERLAP`, `ERHE_FRAME_PIPELINING`), are compared byte for
  byte (a window screenshot has its status bar cropped first). A compare
  that goes through an image library's bounding-box or alpha-only
  difference reports identical for any two opaque images; the compare must
  be exact bytes. Expected noise: one LSB on a handful of pixels in the GI
  bake scenes.
- **Race gate.** `ERHE_USE_TSAN` with `scripts/tsan_suppressions.txt` (one
  exact library name per uninstrumented library), the windowed editor with
  the overlap and the pipelining on, driving the image gate, four view with
  splitter drags, glTF churn, a lightmap bake, DDGI and radiance cascades,
  the developer windows, a soak and exit: zero reports from the main,
  ImGui or render phases (`doc/agents/linux.md` "ThreadSanitizer";
  macOS: `doc/agents/macos.md` "ThreadSanitizer"). Two
  startup and shutdown reports come from uninstrumented third-party code
  (SDL's libdbus mutex, the Vulkan loader in `vkDestroyDevice`).
- **Tests.** `erhe_graphics_gpu_tests` covers two open device frames;
  `erhe_frame_pacing_tests` the recorder; the scene, scene renderer and
  editor renderer tests the snapshots; the rendergraph tests the per-frame
  node list.
- **Frame-time measurement.** `scripts/render_thread_bench.py` runs one
  scene in the `inline`, `dedicated`, `overlap` and `pipelined`
  configurations with vsync off and no MCP polling, in an isolated config
  copy, and averages the phase log over the steady state; `--tracy` records
  a capture per run (a tree with `ERHE_PROFILE_LIBRARY=tracy` and
  `ERHE_TRACY_CALLSTACK_DEPTH=0`, since deep callstacks cost more than the
  frame), `--gpu` picks the Vulkan driver, `--viewport-size` and `--set
  window.json:size` shrink the window to take a GPU-bound frame off the
  GPU. `scripts/tracy_zone_summary.py` lists the hot CPU and GPU zones and
  the phase picture of a capture: how much of R the main thread was busy
  (and of that, in M2) versus blocked, and how much of the tick R was idle.
  `scripts/bistro_frame_bench.py` reproduces a windowed user session on
  the Bistro scene (open over MCP, the session's camera and import root
  transforms, deferred loads settled, a quiet window with no MCP traffic)
  under a Tracy capture and prints the per-frame picture of consecutive
  quiet frames: tick, M1/M2/M3/kick/join, the R each tick kicked, the
  zones above a threshold on both threads and the GPU zones, plus the
  tick's lag-1 autocorrelation (an alternating slow/fast pattern shows as
  a negative value). `--analyze-only` re-reads a saved capture; the
  csvexport output is cached next to the capture. The default scene is
  not in the repository: when it is missing the script downloads the
  `bistro_lights_compressed` archive from its Google Drive share into
  `res/editor/assets/`; `--test-download` exercises that download into a
  temporary directory while a local copy exists.
  `scripts/perfetto_system_trace.py` and `scripts/tracy_to_perfetto.py`
  put a system trace and a Tracy capture side by side in Perfetto. The
  scenes are `config/editor/commands_bench_heavy.json` (about 1500 static
  instances, every light type with shadows) and `commands_bench_draws.json`
  (the same meshes, one shadow light) plus the default scene. The
  measurements taken so far, and how to read them, are in
  `doc/reference/render_thread_measurements_2026_10.md`: on the GPUs
  measured every scene is GPU-bound at the full window, and the Tracy phase
  picture shows R busy while M2 runs in the pipelined schedule (R3).

## 4. Future work

- **Headset verification and the headset prepare phase.** The OpenXR
  overlap (R1) has not been verified on a device: the phase log or Tracy
  should show `xrWaitFrame` of frame N+1 inside R of frame N, with frame
  rate, `XR_FB_performance_metrics`, passthrough, quad layers, controller
  rays and the ID pick as before. The headset path still prepares and
  renders in R from live state (section 2.2); recording its tools in M2
  against the predicted pose and patching the slot's views in R, and taking
  its passes through the prepared composition, would let it run the
  pipelined schedule. The OpenXR runtime submits on the graphics queue in
  `xrEndFrame` outside the device's queue mutex.
- **Upload job queue and OpenGL.** Every M2 GPU-recording site (asset and
  texture upload ticks, `Mesh_memory::flush`, material sets, hotbar init,
  MCP-driven edits, operations) behind an upload job queue the render
  thread drains at the start of R, so the main thread never calls
  `get_command_buffer`, `acquire_frame_slot` moves to R with every submit,
  and OpenGL joins the dedicated mode by making its context current on the
  render thread once (`Context_window::make_current`), the GL worker
  context pool unchanged. The pacing half of the frame start stays on the
  main thread.
- **Metal.** The Metal backend runs the dedicated thread and pipelining
  defaults; its thread-safety table is `doc/erhe/metal_backend.md` "Thread
  safety". On macOS the image gate (default scene, two cameras) is
  byte-identical across the four configurations and the race gate (scene
  renders, screenshots, DDGI and radiance cascades, two glTF opens, soak,
  exit) reports nothing. The bake, wide-line, skinned and four-view cases
  of the gate have not been run on Metal.
- **Measurements still open.** The windowed frame pacer check with the
  present-timing tiers; a CPU-bound measurement on a GPU with a lower floor
  than the ones measured (every measured scene is GPU-bound, so the R3
  gain is shown by the Tracy phase picture, not by the tick).
- **Render phase cost.** `Shadow_renderer::render` dominates R on scenes
  with many casters: the tight directional fit runs once per shadow-mapped
  directional light. Fitting several lights in parallel pays only with many
  casters and many shadow lights; when built, the receiver cache is filled
  before the fan-out and each worker gets its own `Shadow_fit_scratch`.
- **Paths outside the gates.** The region scan was not driven under TSan,
  and its readback sometimes returns an empty or partial id set (timing
  dependent; pre-existing). The standalone main-thread submits outside the
  lightmap baker and streamer (`Texture_renderer::render_and_read_rgba8`,
  the mesh pool read) and the windowed capture path are unaudited for the
  pipelined schedule; `Scene_pass_resources` state set in R (lightmap
  texture, bicubic, DDGI) may be read by previews rendered in M2.
- **Allocation follow-ups.** `Vulkan_submit_info` holds four vectors built
  per `submit_command_buffers` call (two submits per frame now);
  `Device_impl::ensure_device_frame_slot` has no callers;
  `Scene_root::add_trigger_event` formats a string inside the M1 physics
  steps.
- **Frame pacing.** The exact holdback with full-budget scheduling
  (`doc/plans/frame_pacing.md` "Asynchronous present") needs the present on
  a thread separate from recording.
