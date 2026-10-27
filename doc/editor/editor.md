# Editor Application

Stability: mostly stable

## Purpose

The editor is the main application built on the erhe C++ graphics engine. It provides a 3D scene editor with ImGui-based UI, supporting mesh creation, geometry operations (Catmull-Clark, Conway operators, CSG), physics simulation (Jolt), brush-based mesh placement, transform tools, and optional OpenXR headset support.

## Entry Point and Initialization Flow

- **Entry**: `main.cpp` calls `editor::run_editor()`.
- **`run_editor()`** (in `editor.cpp`):
  1. Initializes log sinks and all library-level logging.
  2. Initializes sleep, physics, and Geogram systems.
  3. Optionally initializes RenderDoc frame capture.
  4. Constructs the `Editor` object (a local class in `editor.cpp`).
  5. Calls `editor.run()` to enter the main loop.

- **`Editor` constructor**:
  1. Loads per-library configs from `config/` directory (e.g. `config/erhe_graphics.json`).
  2. Creates a `tf::Executor` for task parallelism.
  3. Creates no-dependency subsystems: `Commands`, `App_message_bus`, `App_settings`, `Input_state`, `Time`.
  4. Creates the OS window and `erhe::graphics::Device`.
  5. Optionally creates OpenXR headset.
  6. Creates GPU subsystems: `Programs`, `Imgui_renderer`, `Debug_renderer`, `Text_renderer`, `Forward_renderer`, `Shadow_renderer`, `Mesh_memory`, `Rendergraph`, `Thumbnails`, `Icon_set`, `Post_processing`, `Id_renderer`.
  7. Creates editor subsystems: `Selection`, `Operation_stack`, `Tools`, `Fly_camera_tool`, transform tools (`Move_tool`, `Rotate_tool`, `Scale_tool`, `Transform_tool`), `Brush_tool`, `Create`, `Physics_tool`, `Paint_tool`, `Material_paint_tool`, `Hover_tool`, `Grid_tool`, `Hud`, `Hotbar`.
  8. Creates all ImGui windows.
  9. Creates a default `Scene_root` and `Scene_builder` to populate it.
  10. Fills the `App_context` struct with pointers to all subsystems.

- **Main loop** (`Editor::run()`): see "Frame phases and the render thread".

### Frame phases and the render thread

The editor renders on a render thread while the main thread runs window
input, the simulation and the OpenXR frame wait. Each frame is three phases,
named in source comments and in `erhe::utility::Thread_roles`
(`doc/erhe/utility.md`):

| Phase | Function | Thread | `Frame_phase` |
|---|---|---|---|
| M1 overlap | `frame_overlap_phase()` | main | `m1_overlap` |
| M2 prepare | `frame_prepare_phase()` | main | `m2_prepare` |
| M3 ImGui | `frame_imgui_phase()` | main | `m3_imgui` |
| R render | `frame_render_phase()` | render role: the render thread, or the main thread in inline mode | `r_render` |

Two schedules decide where the main thread joins R
(`threading.frame_pipelining`, "Mode" below). Pipelined (the default with a
dedicated render thread, and the ImGui overlap): the main thread prepares
frame N+1 while R renders frame N, and waits for R(N) only inside the kick of
N+1, which joins N before it hands over N+1:

```
main:   | M1(N+1) | M2(N+1) | kick: join R(N) | M3(N+1) | publish | M1(N+2) | M2(N+2) | kick: join R(N+1) |
render: | R(N): scene ... ImGui hosts, submit |         R(N+1): scene ... wait | ImGui hosts, submit |  R(N+2)
```

Serial (pipelining off): M1 of the next frame still overlaps R, but the
main thread joins R(N) at the end of M1(N+1), before M2(N+1):

```
main:   | M2(N) | kick | M3(N) | publish | start(N+1), poll_events, M1(N+1) | join R(N) | M2(N+1) | kick |
render: |              R(N): scene ...        | wait | ImGui hosts, submit |   idle   | R(N+1)
```

With pipelining a frame is no longer the sum M1 + M2 + R: the main thread's
own work (M1 + M2 + M3) and R overlap, and the kick blocks only while R(N) is
unfinished (the queue has depth one: at most one frame rendering and one
being prepared). The device frame bracket allows the two open frames: M2(N+1)
acquires the frame slot of N+1 (`Device::acquire_frame_slot`) while R(N)
records into the slot of N; the acquire's timeline wait retires frame N-1,
the slot's previous user, which the kick of N joined. From
`Device::wait_for_frame_start` in M1 on, the main role's
`Device::get_frame_index()` is the frame it prepares.

The device entry points both phases reach (resource creation and
destruction, ring buffer ranges, completion handlers, submits, frame ids)
are safe when M2 and R overlap; `doc/erhe/graphics.md` "Thread safety" lists
each one and its guard.

M1 and M3 run concurrently with R in both schedules; M2 does with
pipelining. Every value R reads that a main-thread phase writes is a copy
made for R's frame ("R reads per-frame copies only" below), so the main
phases edit the live state while R renders. The results R produces for the
main thread are read one frame late ("What R writes for the main thread").
M3 draws the editor windows while R renders the
same frame's scene: the windows read the live editor state they own and the
copies M2 made (render settings, light, joint and per-view snapshots), and
R's ImGui host rendergraph nodes, at the end of the rendergraph, wait for
the draw data M3 publishes (`Render_thread::publish_draw_data` /
`wait_for_draw_data`, through `Imgui_renderer::set_draw_data_wait`). What
they draw is the per-frame copy M3 made of each context's draw data and
texture requests (`doc/erhe/imgui.md` "Threading"), never the live ImGui
context, and the wait is slot-exact: R(N) returns when frame N itself was
published, and verifies no later frame overwrote its entry. A
scene edit made in a window during frame N is picked up by M2 of frame
N+1. Without the overlap (`threading.imgui_overlap` off, or no dedicated
render thread) M3 runs before the kick and the frame is serial.

**`run()`.** Each iteration classifies the window activity
(`Frame_activity`: `hidden` while the session is locked, while an unfocused
window is fullscreen, or with `power_save` while the window is not visible;
`unfocused` with `power_save` while it is not focused; never throttled under
OpenXR), runs the frame start (`start_frame()`: `Device::wait_for_frame_start()`
and the frame pacing steps of M1, so the pacing wait precedes the poll),
polls window events with the wait time that activity implies (`hidden_fps` /
`unfocused_fps`), marks the queued input events for dispatch and calls
`frame()`. The poll is the only one per frame and the last thing before the
frame, so the input M2 dispatches is the newest; a frame started before a
poll that ran no frame is kept for the next iteration (on Android a paused
activity sleeps 50 ms instead of running a frame). `quit_after_frames` ends
the loop after that many frames. When the loop ends, `run()` joins the last
kicked frame and flushes the settings. The live-resize redraw callback calls
`frame()` too, except while a frame is in progress (`m_in_tick`); the first
of its frames takes the frame start `run()` made before the poll, the later
ones make their own.

**`frame()`** runs M1, then (serial) joins the frame kicked by the previous
`frame()`, runs M2, then kicks R and runs M3 (the ImGui overlap) or runs M3
and kicks R, publishing M3's draw data either way. With pipelining the kick
(`kick_frame()`, `Render_thread::kick()`) joins the previous frame first:
it waits for R(N), takes its `Frame_result` and only then hands over N+1.
There is nothing to join on the first frame; when the joined frame reports
`no_swapchain`, `frame()` sleeps 50 ms so the loop does not spin on a surface
that is not ready. The join wait, measured by `Render_thread` (in `join()`
or in `kick()`), is stamped into the time record of the frame being prepared
as its `join_wait` pair, an involuntary wait inside the CPU slot
(`doc/frame_pacing/inputs.md` 3.2). At the join, `on_frame_joined()` adds R's
timings into the phase timing, publishes the joined frame's device frame id
as `App_context::last_rendered_frame_index` (below) and the sky LUT readiness
R reported, and hands the frame's log entries (the frame store both threads
logged into) to the Frame log window (`Frame_log_window::on_frame_end`), so
the window reads main-thread state only. With pipelining all of this
happens at the kick of the next frame, one frame later than the serial
join at the end of M1: M2 of N+1 sees N-1 as the last rendered frame (N in
the serial schedule), M3 of N+1 sees N when it runs after the kick (the
ImGui overlap) and N-1 when it runs before it.

**What R writes for the main thread.** Results the render phase produces
and the main thread reads are per frame in flight
(`renderers/render_frame_results.hpp`, `Render_frame_results<T>`): R writes
the slot of the frame it renders (the role-keyed `Device::get_frame_index()`),
stamped with that frame, and the main thread (M2, M3, MCP) reads the result
of the last rendered frame, `App_context::last_rendered_frame_index`, the
newest frame it joined; a result for a frame in which the owner did not
render is absent (null), never an older frame's. The main thread never
reads the slot R is writing: without frame pipelining it reads frame N
after joining R(N), and M3 under the ImGui overlap reads N - 1 while R(N)
runs; with pipelining M2 of N + 1 reads N - 1 while R(N) runs, and M3 of
N + 1 reads N after the kick (ImGui overlap) or N - 1 before it. Each reader states that
it sees the last rendered frame. Code that runs in both phases (the
renderables and `Debug_visualizations`: the viewports record them in M2,
the headset in R) picks the frame with `App_context::get_results_frame_index()`:
the last rendered frame in a main-thread phase, its own frame in R, whose
producers ran earlier in the frame. `Render_frame_results::write` asserts
the render thread. The results:

- `Shadow_render_node`: `Shadow_frame_result` (the light projections, the
  draw-list shadow statistics, the execute's CPU time). The projections are
  self-contained: their light pointers, view camera and fit settings point
  at copies inside the result, not at the scene's light set slot or the
  view's camera snapshot, which a later M2 rewrites. R reads its own
  frame's (`get_light_projections(frame_index)`, through
  `Scene_view::get_light_projections()`: the composition passes, the ray
  tracer, the depth visualization node, the scene image capture); the
  main thread `get_frame_result(last_rendered_frame_index)`
  (`Debug_visualizations`' light and shadow fit lines in M2, the MCP light,
  shadow fit and composition pass queries).
- `Composition_pass`: `Composition_pass_stats` (the outcome, scene view,
  mesh and draw-list counts and CPU time of the frame's most recent render
  call), recorded only by calls in the render phase; the MCP
  `get_composition_passes` reads the last rendered frame's.
- `Ddgi_renderer::Rendered_state` (whether DDGI is active, the fitted
  grid, the atlas sizes, its resolutions, the measured cost and the newest
  decoded probe state readback) and `Radiance_cascades_renderer::
  Rendered_state` (whether it is active, the layout, the field parameters,
  the texture memory, fit count, cost, applied merge mode, atlas preview
  size and the newest decoded probe overlay copy), published at the end of
  each tick: the probe overlays (M2), the DDGI and Radiance Cascades windows
  and the MCP indirect diffuse stats read the last rendered frame's. Large
  vectors are copied into a slot only when a newer readback was decoded.
- `Ray_trace_renderer::Rendered_output` (the output size and instance
  count of the frame's trace), read by the Ray Trace window and the MCP
  `set_ray_trace` result.
- CPU time totals keep running in R; a reset from the main thread
  (`reset_composition_pass_stats`) takes the last rendered frame's totals as
  the baseline the reported totals subtract.
- Small values travel in `Frame_result`: `Sky_renderer`'s LUT readiness,
  which gates the lightmap bake's sky term in M2 (the LUT textures are
  created once by the constructor; R records their contents).

**M1** writes nothing R reads; what R needs from it travels in the packet.
In order:

1. The frame start, when `run()` has not made it before its poll
   (`start_frame()`): `Device::wait_for_frame_start()`, the pacing half of
   the device frame start (in the position of `xrWaitFrame`); the packet
   gets the device frame id. Then the frame pacing steps (`frame_start_pacing()`: observer
   tick, FR2 release gate, tier S slop servo, FR4 display advance; the FR3
   target and holdback deadline go into the packet, and so does the FR5
   present-wait id when pacing is enforced (it reads the main-thread
   `Frame_activity`) and the decision is for this frame, -1 otherwise;
   `doc/frame_pacing/requirements.md` "Frame phases") and the Frame Pacing
   window's collection. With `editor.frame_pacing` at debug, each frame logs
   the frame id the display advance was computed for next to the frame id it
   is applied to; they are equal.
2. `Time::prepare_update` with that display advance; the packet gets the
   frame number and host time.
3. OpenXR: `Headset_view::wait_frame()` (`xrWaitFrame`; skipped when the
   previous M2's event poll failed). The `Frame_timing` goes into the packet;
   a failed wait sleeps 250 ms.
4. `App_scenes::before_physics_simulation_steps` (the node -> body writes
   the transform observers queued since the previous frame, plus the
   kinematic physical re-targets; `Node_physics_system::apply_pending_node_poses`),
   then one `update_physics_simulation_fixed_step` per fixed step of
   `Time::for_each_fixed_step`. Both hold
   `Scene_root::get_physics_step_mutex()`; R code that reads body state (the
   physics debug visualization, the drag point body display) takes the same
   mutex. Body activation changes the steps report are queued by
   `Node_physics_system` for M2.

**M2**, in order:

0. The upload command buffer opens (`current_command_buffer`), the shared
   empty material set publishes its copy for the frame (passes with no
   material source bind it, the previews below among them), then the
   operations the windows and menus deferred out of the previous frame's
   ImGui iteration run (`Imgui_windows::flush_queue`;
   `doc/editor/coding_rules.md` "ImGui-time work").
1. Dispatch the input events the last `poll_events` queued, once per poll (a
   live-resize redraw frame, run from inside `poll_events`, finds them
   dispatched already); a resize event raises the resize request; on Android
   a new native window recreates the surface (with pipelining, after
   joining the outstanding frame: the swapchain is R's).
2. `Device::acquire_frame_slot(frame_id)` with the packet's device frame id
   (the resource half: timeline wait and pool resets for the slot). In the
   serial schedule R of the previous frame has ended, so this is the only
   open frame; with pipelining R(N) may still record frame N in the other
   slot.
3. `should_render` goes into the packet: false while the window is hidden.
   M2 touches neither the swapchain nor the frame command buffer; both
   belong to R.
4. The upload command buffer is allocated from the main upload slot (thread
   slot 1), begun, and published as `App_context::current_command_buffer`
   until the end of M2. It records on hidden frames too (deferred hotbar
   init, MCP edits, operations).
5. `Scene_commit_queue::flush`, `Asset_manager::tick`,
   `Texture_file_loader::tick`.
6. Transform animations, `Animation_player::update` (by the routed display
   advance when there is one, the wall-clock frame duration otherwise),
   `Fly_camera_tool::on_frame_begin`, the viewport pointer update (desktop).
7. OpenXR: `Headset_view::poll_events()` (`xrPollEvent`; the session state
   transitions it applies are read by R), then, when the wait succeeded,
   `Headset_view::update_actions(timing)` (`xrSyncActions`, hand tracking,
   view pose, controller nodes and pointer); a failure sleeps 250 ms.
   The headset's `Scene_view::Render_view` of the frame (scene root and
   root camera; neither while the headset is not homed in a scene) is
   copied later, by `Headset_view::prepare_render_view()` right after the
   scenes' render settings snapshot, every frame; the headset's
   `Shadow_render_node` and `render_headset()` read it in R.
8. The node-writing fixed steps over the same step list
   (`Headset_view::update_fixed_step`, `Fly_camera_tool::update_fixed_step`),
   then `App_scenes::after_physics_simulation_steps` (pause / resume edge,
   queued body activations, body write-back).
9. ImGui events, `Commands::tick`, `Mcp_server::process_queued_requests`,
   network update, hover reset, hotbar rebuild, the ImGui windows
   (`Imgui_windows::begin_frame` / `draw_imgui_windows` / `end_frame`),
   settings autosave; on rendered frames `Thumbnails::update`, which
   renders the material and brush previews the windows requested into the
   upload command buffer (the ImGui hosts sample them after the GPU edge);
   hover info.
10. `Operation_stack::update`, geometry graph and texture graph evaluation,
    closed editor windows, `Asset_manager::flush_pending_removals`,
    `App_message_bus::update`, async task purge, scene-close leak watches,
    `Fly_camera_tool::on_frame_end`.
11. The lightmap commit work (`lightmap_commit_phase()`): the partition
    commit (`Lightmap_partitioner::update`), the Lightmap window's deferred
    requests (`Lightmap_window::update`) and the streamer reload after an
    offline bake (`doc/editor/lightmap_baking.md` section 3a "Frame phases").
12. `Mesh_memory::flush`; the resize request and window size go into the
    packet.
13. Layout nodes, scene view and headset transforms, hotbar placement,
    display color and draw mode proxy rebuilds, `App_scenes::flush_draw_lists`.
    Rendered frames: `Scene_views::prepare_render`, every
    viewport's `Viewport_scene_view::prepare_render`: it fills the view's
    render state copy of the frame (`Viewport_render_state`,
    `scene/viewport_render_state.hpp`: scene root and camera as strong
    references, viewport, shader debug, hover flag, pointer position, pick
    probe, the Debug Visualizations shadow texel toggle and
    `Weight_display`'s joint weight ramp inputs) and the `Scene_view`
    render view copy of the frame (`Scene_view::Render_view`: scene root,
    camera, shadow fit override camera, viewport config and camera
    viewport; `get_render_scene_root()`, `get_render_camera()` as
    `erhe::scene::Camera_snapshot`, `get_render_config()`), then the
    `render_scene_view` message, `Tools::render_viewport_tools` and
    `App_rendering::render_viewport_renderables` (the CPU phase, no
    encoder) record the view's lines and labels into one debug / text
    renderer frame slot, which the render state names, and the grid tool
    records the view's grid into it (`Viewport_scene_view::record_grid`;
    the grid composition pass draws each view's own grid). Both copies
    exist once per frame in flight, indexed by the role-keyed
    `Device::get_frame_index()`: the prepare phase fills the copy of the
    frame it prepares, the render phase reads the copy of the frame it
    renders (`Viewport_scene_view::get_render_state(frame_index)`; the
    `Scene_view` getters key by the calling role's frame; both verify that
    the frame copied the view). The render state also carries the view node's enabled flag
    as the prepare phase saw it: the composition is prepared, and the render
    phase renders the view, only when it was enabled then (the Viewport
    window sets the flag while it draws, after the prepare phase; step 15's
    `Rendergraph::prepare_frame` copies it for the rendergraph). `Scene_image_view` copies its camera at the request and
    publishes that copy, with the frame's scene root, in its
    `prepare_composition`; the headset copies its render view in its own
    render phase update. The render phase reads the copies: its
    `Render_context::camera` is null, `camera_snapshot` is the copy,
    `Render_context::scene_root` is the copy's scene root (R reads no
    `Scene_view::get_scene_root()`) and `Render_context::render_state` the
    viewport's render state; the tools' CPU phase has both. The tools
    therefore see this frame's transforms, hover and selection, and the
    debug visualizations that read a shadow node's light projections see
    the previous frame's. Then material set updates: every scene root's
    forward and draw-list set updates once, written or clean, which
    publishes the buffer copy and texture heap the render phase of this
    frame binds (`doc/erhe/draw_list_material_set.md` D9).
    Before the tools record: the render settings snapshot. `Render_settings`
    (`renderers/render_settings.hpp`, `App_context::render_settings`) copies
    the editor settings sections and graphics settings the render phase
    reads (sky, shadow frustum fit, selection outline, render style
    appearances, debug visualizations style, DDGI, radiance cascades and
    ray trace configs, content edge lines, exclude unlit, indirect diffuse source, the graphics
    preset, use draw lists, headlight when unlit, sky enabled) into the
    copy of the frame being prepared: `Render_settings_frames` (owned by
    `Editor`) holds one copy per frame in flight. `App_context::
    render_settings` (`Render_settings_access`) resolves to the calling
    role's copy at each access: the dedicated render thread reads the copy
    of the frame it renders (role-keyed `Device::get_frame_index()`,
    verified to be the frame that filled it), the main thread the latest
    snapshot (M1 and M2 before the snapshot the previous frame's, M2 after
    it and M3 this frame's; in inline mode R runs on the main thread right
    after its own M2, so the latest is its frame's). The DDGI, radiance
    cascades and ray trace renderers fetch their config sections through it
    at each use and keep no reference into a copy. Every scene
    root resolves its overrides into its `Scene_render_settings` copy
    (`App_scenes::snapshot_render_settings`), which also copies the scene
    facts the render phase reads: the ambient light, whether the light and
    content layers are empty (the shadow node's headlight and early-out
    decisions), the sky's sun direction from the last directional light
    (`Sky_renderer::resolve_sun_direction`), the trace camera, the resolved
    light set (`erhe::scene_renderer::Light_set::resolve` under the
    preset's light count limits, every slot an `erhe::scene::Light_snapshot`
    refreshed each frame) and the posed joints of the scene's skins
    (`erhe::scene_renderer::Joint_poses`; the capture assigns each skin's
    joint slot). Each of the three is one copy per frame in flight, filled
    for the frame being prepared and read by the frame index
    (`Scene_root::get_render_settings(frame_index)`,
    `get_light_set(frame_index)`, `get_render_joint_poses(frame_index)`,
    each verifying that frame filled it): the main thread passes the
    prepare frame (role-keyed `Device::get_frame_index()`), R its render
    frame, so the prepare phase of the next frame never writes what R
    reads. Every frame's light set has its own layout state: a light hook
    (register / unregister / `on_light_changed`, any thread) bumps the
    root's invalidation serial under a mutex, and each set re-resolves its
    layout at its own frame's resolve when the serial moved. The preview
    roots, which the frame loop does not prepare, make the same copies at
    their render entry (`Scene_root::snapshot_preview_render_settings`).
    Beside the snapshot, in this order, each scene root
    gathers the shadow frustum fit's caster and receiver bounds of its
    content layer into the draw list scene's slot for the frame
    (`Scene_root::prepare_shadow_bounds`, under `item_host_mutex`;
    `Draw_list_scene::prepare_shadow_bounds`) and publishes its draw lists
    into the frame's slot (`Scene_root::publish_draw_lists`, under
    `item_host_mutex`; `Draw_list_scene::publish`: the skinned records take
    the joint slots the capture just assigned, then the lists that changed
    since the slot's last publish are copied). The render phase reads the
    copies only (the DDGI, radiance cascades and ray trace renderers fetch
    their config sections from the frame's copy at each use, the content
    wide-line renderer takes the edge-line tent settings from it in
    `set_view_params`; the light projections, light buffer, shadow passes and
    joint buffer read the snapshots, the draw-list shadow path the bounds
    slot, the draw-list draws the published lists), so a window's edit of a live setting,
    light or joint reaches the render phase at the next snapshot, and the
    ImGui phase of step 11g may run while the render phase reads them.
    After every viewport's `prepare_render`, `Id_renderer::begin_frame`
    hands the region scan the tools requested to the prepare frame
    (`has_frame_scan`); step 15 puts it into one view's prepared id pass,
    and the render phase reads it from there. Preview roots outside
    `App_scenes` resolve their light set at their own render entry, and the
    headset still captures its joint poses in R after its own transform
    update (the headset records against poses located in R).
    After the thumbnails, the requests the windows made at ImGui time are
    served: `Material_preview::update` renders the Properties preview,
    `Brdf_slice::update` and `Depth_visualization_window::update` apply
    material, enabled and connections to their render graph nodes and
    request the node's size for the frame
    (`Texture_rendergraph_node::request_size`, one request per frame in
    flight). `prepare_render` copies the view's content render target size
    and MSAA sample count into the frame's `Viewport_render_state`, and
    every shadow node copies its requested configuration, with the render
    settings copy's shadow technique, into the frame's slot
    (`App_rendering::prepare_shadow_render_nodes` ->
    `Shadow_render_node::prepare`; a graphics preset change requests a new
    configuration through `Shadow_render_node::request_configuration`).
    M2 decides, R applies: every resizable GPU object R uses is created,
    replaced and dropped by R only, at the start of the owning node's
    execute - the view's content and overlay render targets
    (`Render_target::reconfigure` / `update`,
    `update_overlay_render_target`) from the frame's render state, the
    BRDF slice and depth visualization targets
    (`Texture_rendergraph_node::apply_requested_size`), the shadow maps and
    their render passes (`reconfigure` with the frame command buffer for
    the layout transitions), and each post-processing pyramid from its
    input texture, which the producer node sized earlier in the same R
    (`Post_processing_node::update_size`; the old pyramid is released at
    the end of `Rendergraph::execute` through `defer_resource`). A window
    that shows such a texture in ImGui passes the node itself as the
    texture reference, resolved when the ImGui host renders in R; the
    ImGui phase does not read these textures.
14. Rendered frames: the lightmap plan (`lightmap_prepare_phase()`): paused
    change detection, `Lightmap_baker::prepare_tick` (into the tick slot of
    the frame being prepared) or the streamer's residency, tile uploads and
    mapping writes, and the lightmap binding
    (`App_rendering::set_lightmap_texture`). Every frame, after it, the
    binding is copied into the packet (`Frame_packet::lightmap_texture`),
    and so is the Ray Trace window's switch
    (`Frame_packet::ray_trace_enabled`).
15. Rendered frames, the last step that reads the scene: the composition of
    every view that draws the composer in R
    (`App_rendering::prepare_composition`: each viewport and each live
    `Scene_image_view`, under its scene root's `item_host_mutex`). For
    every composition pass the view draws, `Composition_pass::prepare`
    evaluates the gating (`enabled`, the `is_enabled` predicate, the view's
    render style, the wide-line and draw-list routing, the primitive
    settings, with the selection outline pulse at the packet's host time)
    and, for a pass on the bucket path, `Forward_renderer::prepare` buckets
    its meshes and writes their complete primitive records and draw
    commands (`erhe::scene_renderer::Render_bucket_snapshot`); the sky
    pass parameters for the view's scene are set first
    (`App_rendering::update_sky_parameters`), and the grid and sky
    parameters are copied with the pass. The result goes into the view's
    `Composer_frame_slots` slot of the frame in flight, together with the
    view's id pass when the view renders one (hovered, a region scan
    pending since `Id_renderer::begin_frame`, or a pick probe armed;
    `App_rendering::prepare_id` -> `Id_renderer::prepare`: records, draw
    commands, the id range table and the region scan; the first view that
    prepares an id pass claims the scan with `Id_renderer::take_frame_scan`,
    so the scan is drawn once per frame, and R gates the scan on the
    prepared pass's `scan`, never on the renderer) and the edge-line feed of the view's
    wide-line passes (`Viewport_scene_view::prepare_wide_line_feed`: the
    selection outline, edge line, translucent outline and ghost edge line
    passes, gated as they draw, each mesh through
    `Content_wide_line_renderer::prepare_mesh` into the slot's
    `Content_wide_line_feed`; R queues it with `add_feed`, runs the compute
    pre-pass and draws). Then every shadow node prepares its
    bucket path casters when the draw-list gate is off
    (`Shadow_render_node::prepare_casters` -> `Shadow_renderer::prepare`,
    into the node's slot of the frame in flight; the pass variant bits are
    ORed in per pass in R). It runs last
    because the records carry the forward material set slots
    (`update_material_sets`), the lightmap uv transforms (step 14) and
    every transform, flag and mesh this M2 changed. R draws the slot
    (`Composer::render` with `Render_context::composer_prepared`, which
    verifies the slot's frame): it reads no pass gating state and, on the
    bucket path, no `Mesh`, `Node`, `Primitive` or `Material`; it applies
    only the environment shader-key axes it owns (light counts, shadow
    axes, multiview count, DDGI). The previews, which render in M2, and
    the headset, which renders in R (`doc/editor/render_thread.md` section 2.2),
    prepare each pass where they render (`Composition_pass::render_immediate`;
    the headset's pick pass, `Id_renderer::render_immediate`).
    Then, still on rendered frames, `Rendergraph::prepare_frame`
    (`doc/erhe/rendergraph.md`): every enabled node's
    `prepare_rendergraph_node` hook copies what its execute reads into the
    node's slot of the frame (`Post_processing_node`: the
    `Post_processing_parameters` the Post Processing window edits;
    `Brdf_slice_rendergraph_node`: the material and the Properties window's
    phi / incident phi, and its own material set's sync, record write and
    publish, with the material's slot; `Scene_image_readback_node`:
    disables the capture chain once a render phase recorded its readback,
    which with pipelining the prepare phase may learn one frame late, so the
    chain runs once more and that frame's readback node records nothing),
    then the node list (in execution order) is copied into the frame's
    slot and every node's live enabled flag and connections into its
    record of the frame. `Rendergraph::execute` runs the nodes of its
    frame's slot enabled in their records, without the graph mutex; an
    enable or disable from the ImGui phase (the Viewport window,
    `Quad_view::set_visible`), a new node or connection reaches the next
    frame, and a node released while a frame not yet rendered names it is
    destroyed once that frame is rendered (`doc/erhe/rendergraph.md` "Node
    lifetime"); its release hook (`Rendergraph_node::on_released`) removes
    it at once from the registries a prepare or ImGui phase iterates (an
    `Imgui_host` from `Imgui_renderer`'s hosts, a `Scene_image_view` from
    `App_rendering`'s views). The slots are `Prepared_frame_values<T>`
    (`renderers/prepared_frame_values.hpp`), the M2 -> R counterpart of
    `Render_frame_results`.
16. Every frame, hidden ones included, when the device supports ray
    queries: each scene root's `Ray_trace_scene` is brought up to date and
    published into the frame's slot
    (`App_scenes::prepare_ray_trace_scenes` ->
    `Scene_root::prepare_ray_trace_scene`, under `item_host_mutex`; see
    [`raytrace.md`](raytrace.md) "Ray_trace_scene"). It follows
    `update_material_sets` because the instance records carry forward set
    slots. R's ray trace, DDGI and radiance cascades consumers read only the
    slot.
17. The upload command buffer ends and `current_command_buffer` is reset to
    null.

`frame()` then sets the main phase to `none` and kicks R (with pipelining
the kick first joins R of the previous frame).

**M3**, in order: `Hover_tool::reset_item_tree_hover`, then
`Imgui_windows::begin_frame` (with the packet's device frame index),
`draw_imgui_windows` and `end_frame` for every host (which copies each
host's draw data into the slot of that device frame,
`Imgui_host::copy_draw_data`), then the editor settings autosave. Windows
that show textures R creates or replaces pass a reference R resolves
(`Rendergraph_node`, `Post_processing_node_texture_reference`, the Render
Graph window's per-output references, and `Render_texture_reference`
(`renderers/render_texture_reference.hpp`) for the DDGI atlases, the
radiance cascades atlas preview and the ray trace output) and size them
from what R published last (`Post_processing_node::get_pyramid_info`, the
size a reference resolved last, the DDGI and radiance cascades
`Rendered_state` and `Ray_trace_renderer::Rendered_output`), a
one-frame-late copy. The Lightmap Texture window and the Properties
window's texture preview show textures the main thread creates (the baker's
atlas and G-buffer in M2, content library textures), which R reaches only
through the copies M2 makes for it (the lightmap tick slot, the packet's
atlas binding), so they pass the texture itself. `current_command_buffer` is null and
`Thread_roles::assert_command_recording_allowed` refuses the main thread in
this phase, so a window that still records GPU work aborts instead of racing
R; the deferral rules are in `doc/editor/coding_rules.md` "ImGui-time work".

**R**, in order:

1. OpenXR: `Headset_view::begin_frame(timing)` (`xrBeginFrame` with the
   packet's `Frame_timing`). The frame command buffer comes from the render
   slot (thread slot 2); the render role's device frame is the packet's,
   which R verifies. The FR5 present-wait clamp (`present_wait_clamp()`,
   `Device::wait_for_displayed_frame` with the packet's present-wait id),
   then, unless the window is hidden or OpenXR owns the display,
   `wait_for_swapchain` on the frame command buffer. Both run here because
   `VkSwapchainKHR` is host-synchronized in `vkWaitForPresentKHR`,
   `vkAcquireNextImageKHR` and `vkQueuePresentKHR`, and R owns the
   swapchain. A failed wait (no swapchain image: an Android surface between
   background and foreground) makes the frame a non-rendered one: no
   acquire, rendergraph or present; the uploads and the frame command
   buffer are still submitted device-only, the frame slots released and
   the device frame ended, and the `Frame_result` reports `no_swapchain`.
   Then `Device::set_present_target_time` with the
   packet's FR3 target and holdback deadline, so the device fields the
   present path reads are written by R only.
2. RenderDoc capture start (when requested).
3. The frame command buffer begins; the upload command buffer gets a GPU edge
   to it (`signal_gpu_completion_to` / `wait_for_gpu_completion_signal`) and
   is submitted first, device-only. Under OpenXR the frame command buffer is
   the one `Xr_session::render_frame*()` chains its view command buffers
   after, so the edge covers them too.
4. The render-phase host time is published from the packet
   (`App_context::render_phase_host_system_time_ns`, read by the selection
   outline pulse), then the shader monitor update. Before the frame command
   buffer is taken, the packet's lightmap binding and ray trace switch go
   to their renderers (`Forward_renderer::set_lightmap_texture`,
   `Ray_trace_renderer::set_render_enabled`).
5. Desktop: `begin_swapchain` (acquire).
6. `apply_frame_settings()` of the DDGI and radiance cascades renderers:
   the source selection and the merge mode of the frame's
   `Render_settings` copy are applied to the render-phase state (a producer
   deselected since the previous frame releases its textures; the change
   reaches R only as the frame's copy, so each compares it with what the
   previous frame applied). Then the radiance cascades and DDGI ticks,
   publication of the indirect
   diffuse field, the irradiance query, the recording of the lightmap tick
   slot M2 filled for this frame (`Lightmap_baker::record_tick`).
7. `Rendergraph::execute`: viewports (the debug renderer compute of the
   slots recorded in M2, the scene, the renderables' encoder phase, the
   slot's lines and text), shadow nodes, ImGui hosts (each waits for M3's
   draw data first, `draw_data_wait` in the phase log, runs the queued
   ImGui texture requests and draws its copy of the frame; the window host's
   swapchain render pass was sized from the packet's window size right
   after `begin_swapchain`) and the headset node,
   which records its own tools in R, renders the views, submits their
   command buffers and calls `xrEndFrame`. Then `Debug_renderer::release_frames`
   and `Text_renderer::release_frames` release the frame being rendered:
   both renderers keep their slots and prints once per frame in flight,
   keyed by the role-keyed `Device::get_frame_index()`, so the prepare
   phase of the next frame records into another copy.
8. `Imgui_renderer::next_frame`, `Headset_view::end_frame` (action change
   flags), `Id_renderer::next_frame`.
9. The frame command buffer ends and is submitted (with the implicit present
   on the desktop); under OpenXR `render_frame*()` has submitted it already.
10. `Device::end_frame(frame_id)` with the packet's device frame id; a
    failure aborts (`ERHE_VERIFY`) on the thread
    running R.
11. RenderDoc capture end, "Main loop: completed frame N" for frames 1 to
    12 (`log_startup`; run scripts wait for frame 12). The frame log entries
    are taken at the join (above), not here.

R records into the frame command buffer, which it passes explicitly
(the DDGI / radiance cascades ticks, `Lightmap_baker::record_tick`, the
rendergraph). It does not read
`App_context::current_command_buffer`, which belongs to the main thread
(the upload command buffer during M2, null otherwise; M1 asserts null).

**The handoff** is `Render_thread` (`render_thread.hpp`). `Frame_packet` is
fixed-size, owned by `Editor` and rewritten each frame: frame number,
device frame id, FR4 display advance, FR3 target and holdback deadline, FR5
present-wait id, host times, the fixed-step dt sum, the OpenXR
`Frame_timing`, the upload command buffer, `should_render`, the
`Frame_begin_info` (resize request), the window size, the lightmap
atlas binding, which R hands to the forward renderer
(`Forward_renderer::set_lightmap_texture`) before it takes its frame command
buffer, and the ray trace switch (`Ray_trace_renderer::set_render_enabled`).
`kick()` copies it, so the next frame rewrites the editor's copy while
R renders. The `Frame_result` of a frame comes back at its join, from
`join()` (serial, and after the frame loop) or from the next `kick()`
(pipelining; nothing on the first frame): the device frame id R
rendered, R's duration, its
`wait_for_swapchain` and `begin_swapchain` times, its draw data wait, the
handoff latencies (kick to R's start; the later of R's end and the start of
the main thread's wait to the join), the join wait's begin and end,
`no_swapchain` and the sky LUT readiness. One mutex and three
condition variables carry the handoff (kick, done, draw data published). An exception in R is logged on `editor.frame` with the frame
number and rethrown on the render thread.

**Mode.** `threading.render_thread` in the editor settings picks the mode at
startup: `dedicated_thread` (the default) runs R on a thread owned by
`Render_thread`, which publishes itself as the render role before the
constructor returns and is named "Render thread" in Tracy;
`inline_main_thread` runs R on the main thread inside `kick()`, and the join
returns at once. `ERHE_RENDER_THREAD=0|1` overrides the setting (0 inline, 1
dedicated). The OpenGL and null graphics backends always run inline. The
startup log names the mode and its source ("Render thread mode: ..."); the
mode is fixed for the session. `threading.imgui_overlap` (default on;
`ERHE_IMGUI_OVERLAP=0|1` overrides it) runs M3 after the kick with a
dedicated render thread; the startup log names it ("ImGui overlap: ...").
`threading.frame_pipelining` (default on; `ERHE_FRAME_PIPELINING=0|1`
overrides it) selects the pipelined schedule; it is forced off without a
dedicated render thread (the OpenGL and null backends, or
`inline_main_thread`) and with an OpenXR headset, whose path still prepares
and renders its views in R from live state (`doc/editor/render_thread.md`
section 2.2); the startup log names it ("Frame pipelining: ..."). Off keeps the
serial schedule for bisecting. The XR view command buffers come from the
render slot (`Xr_session::set_command_buffer_thread_slot`, set at the end of
the constructor; the init pump before it records on slot 0).

**R reads per-frame copies only.** Every value R reads that a main-thread
phase writes reaches it as a copy made for its frame: a `Frame_packet`
field, a per-frame-in-flight slot keyed by the role-keyed
`Device::get_frame_index()` (`Render_settings_frames`,
`Viewport_render_state`, `Scene_view::Render_view`, the scene root's
render settings, light set and joint poses, the draw list, bucket, shadow
bounds, ray trace scene, lightmap tick and composer slots, the debug and
text renderer frames, the rendergraph nodes' enabled flags and
`Prepared_frame_values` slots), or a value R itself owns. The main thread
therefore edits these live, in any of its phases, and the edit reaches the
next frame the prepare phase copies: the editor settings
(`Editor_settings_config`: render styles, debug visualizations style,
content edge lines, DDGI / radiance cascades / ray trace configs, the
indirect diffuse source, the radiance cascades merge mode), the graphics
settings, scenes, lights, cameras and materials (through the M2
snapshots and publishes), a rendergraph node's enabled flag
(`Rendergraph_node::set_enabled`), the post-processing parameters, the
BRDF slice angles and material, the ray trace switch, the lightmap
sampling mode (`Render_settings::lightmap_bicubic_sampling`, handed to
`Forward_renderer::set_lightmap_bicubic` at the start of R), the scene the
indirect diffuse ticks run on (`Frame_packet::indirect_diffuse_scene_root`,
from `App_scenes::get_single_scene_root`), the rendergraph's node list and
connections, the debug renderer's line bias
margin and anti-aliasing (copied at the first `begin_frame` of a device
frame), and the requests the render phase takes under a mutex
(`Ddgi_renderer::request_probe_states`,
`Radiance_cascades_renderer::request_preview`). `Id_renderer::
covers_all_meshes` is fixed at construction. Still read live from R:
the headset path, which prepares and renders in R
(`Composition_pass::render_immediate` with the live `Material_set` slot
table and pass predicates, the tool recording, the hover entries; a
debug renderer frame the headset claims in R copies the live line
settings there).

**MCP diagnostics that read R's output** hand the request to R and read the
answer R recorded once its frame has retired on the GPU, across MCP passes
(the request defers until then); the request state each shares with R is
under a mutex, since with pipelining R records while M2 runs the MCP
requests: `render_scene_image` (`Scene_image_readback_node` owns the
readback buffer and the record of what R copied, with the view's shadow
results of that frame; a node retired after its capture was abandoned
touches nothing destroyed), `set_ray_trace` `save_path`
(`Ray_trace_renderer::request_output_readback`, recorded after the
rendergraph by `record_output_readback`), the irradiance and reference
queries (`Ddgi_renderer`) and the radiance cascades texel copy. Their
argument checks read the last rendered frame's `Rendered_state`, not the
renderers' live state. `capture_screenshot` reads the composited image of
the newest frame that has ended (`Device_impl::is_frame_ended`; headless:
the emulated swapchain records which frame composited which image;
windowed: an armed capture is collected only after its frame ended, and no
capture is re-armed while one waits to be collected).

**Thread ownership asserts** (`Thread_roles`, no-ops until the roles are set):
the render thread in `r_render` for `Rendergraph::execute`,
`Draw_list_scene::draw_color` / `draw_shadow` / `get_shadow_bounds`,
`Imgui_renderer::render_draw_data`, `Headset_view::render_headset`,
`Xr_session::begin_frame` / `render_frame*` / `end_frame` and
`Lightmap_baker::record_tick`; the main thread in
`m2_prepare` (or outside a frame) for `Draw_list_scene::flush_pending` /
`prepare_shadow_bounds`,
`Imgui_windows::begin_frame`, `Operation_stack::update`,
`App_message_bus::update`, `Scene_root::set_physics_simulation_running` /
`apply_queued_body_activations` / `after_physics_simulation_steps`,
`Lightmap_partitioner::update` / `commit_prepare`, `Lightmap_window::update`,
`Lightmap_baker::prepare_tick` / `monitor_paused_scene` / `publish_regions`
and `Lightmap_streamer::update` / `reapply_regions` (and its mapping writes);
the main
thread in `m1_overlap` (or outside a frame) for `Xr_session::wait_frame`; and
no command buffer on the main thread in `m1_overlap`
(`Device::get_command_buffer`). The main thread sets the main phase at the
edges of M1 and M2; `frame_render_phase()` sets the render phase from the
thread that runs it.

**Phase timing.** Every 60 frames `editor.frame` logs `Phase timing (us/frame,
avg of 60 frames)`, averages per frame of: `M1`; `frame_start_wait`
(`Device::wait_for_frame_start` plus the frame pacing steps and their
waits, run by `start_frame()` before `run()`'s poll, so outside M1 unless
`frame()` makes its own frame start); `M2`, which includes `slot_acquire`
(`Device::acquire_frame_slot` alone) and is the sum of six consecutive terms
(`start`: input dispatch and slot acquire; `upload`: scene commit flush and the asset and texture file load
ticks; `sim`: animations, pointer, OpenXR poll and actions, node fixed steps
and physics write-back; `imgui`: ImGui events, commands, MCP, network, ImGui
windows and settings autosave; `update`: hover, operations, graphs, message
bus and lightmap commit; `scene`: mesh memory flush, layout, transforms,
hotbar, draw list flush, material sets and lightmap prepare); `R`; `swapchain`
(`wait_for_swapchain` plus `begin_swapchain`, both in R); `join_wait`, the
main thread's time blocked joining R of the previous frame (in `join()` at
the end of M1, or inside the kick with pipelining; R minus `join_wait` is
the part of R that overlapped the main thread); `handoff`, the kick and join
latencies; and `full tick`, from the start of the frame start
(`frame_start_wait`) through `run()`'s `poll_events` wait to the end of
`frame()` (the kick, or M3 after it with the ImGui overlap): the main loop
period. With pipelining the R terms of a line belong to the frames joined
in that interval, one frame behind the main thread's terms. The
`poll_events` wait is in no term but the full tick.

**Benchmark mode.** With `--benchmark <file>` (`Benchmark_driver`,
`src/editor/benchmark_driver.{hpp,cpp}`) the end of `frame()` also hands
the frame's phase terms (the per-frame deltas of the accumulators above)
and its wall time to the driver, `start_frame()` takes the frame's
simulation advance from the driver instead of the pacer, and M1 sets the
headset's head pose from the driver's camera path before `xrWaitFrame`;
the 60-frame log line is unchanged. The mode, its phases, determinism
rules and report are specified in `doc/plans/quest_performance.md`
"Benchmark mode"; running it on the Quest is in `doc/agents/quest.md`
"Benchmark runs". A benchmark run compiles optimized SPIR-V
(`Graphics_config::spirv_optimization`, doc/erhe/graphics.md "SPIR-V
optimization") unless `ERHE_SPIRV_OPTIMIZATION=0` turns it off.

**Shader variant browser.** `--shader-variant-browser <directory>` records
every shader program the session compiles into an HTML browser in that
directory (doc/erhe/graphics.md "Shader variant browser"): open
`<directory>/index.html`. Programs served from `spirv_cache/` are not
compiled, so delete it first to record every variant.

**Watchdog and shutdown.** The main-loop stall watchdog
(`doc/agents/debugging.md` "Main-loop stall watchdog") watches while
`m_in_tick` is set: from M1 start to the end of `frame()`, the kick's wait
for R included. It keeps the main and render roles' phases apart, so with
pipelining it names main in M2(N+1) and render in R(N). `~Editor` stops the watchdog,
joins a frame still outstanding (`run()` left by an exception), calls
`Render_thread::stop()` (which ends the dedicated thread), and only then
quiesces the GPU (`wait_idle()`) and destroys the parts.

## Key Subsystems and Interactions

### App_context (Service Locator)

`App_context` is a plain struct of raw pointers to all major subsystems. It is filled after construction and passed by reference to subsystems that need cross-cutting access. This is the central wiring mechanism -- subsystems do not hold direct references to each other at construction time but look them up through `App_context`.

### App_message_bus (Event System)

`App_message_bus` holds typed `erhe::message_bus::Message_bus` instances for decoupled communication. Messages include:
- `Selection_message` -- selection changed
- `Hover_scene_view_message` -- pointer entered/left a scene view
- `Hover_mesh_message` -- pointer hovers over a mesh
- `Graphics_settings_message` -- graphics preset changed
- `Node_touched_message` -- a node's transform was modified
- `Load_scene_file_message` -- scene lifecycle
- `Tool_select_message` -- active tool changed
- `Render_scene_view_message` -- scene view rendering requested

Messages can be synchronous (`sync_only`), queued (`queue_only`), or both. Queued messages are flushed once per frame via `App_message_bus::update()`.

### Scene (`scene/`)

`Scene_root` owns an `erhe::scene::Scene`, physics world, raytrace scene, mesh layers, and a `Content_library`. Multiple `Scene_root` instances are managed by `App_scenes`. `Scene_view` is the abstract base for anything that looks at a scene (camera + hover state). `Viewport_scene_view` is the concrete implementation that renders into the render graph.

### Operations (`operations/`)

Undo/redo system. `Operation` is the abstract base with `execute()` and `undo()`. `Operation_stack` queues operations and executes them during `update()`. Concrete operations include geometry operations (Catmull-Clark, Conway operators, CSG), item insert/remove, transform changes, and material changes. `Mesh_operation` is the base for geometry-modifying operations.

### Tools (`tools/`)

`Tool` extends `Command_host` with priority-based activation, viewport rendering, and tool properties. `Tools` manages the collection and priority system. The currently active ("priority") tool gets highest input priority. Tools include:
- `Fly_camera_tool` -- camera navigation (WASD + mouse)
- `Selection_tool` -- click-to-select
- `Brush_tool` -- place brushes on surfaces
- `Physics_tool` -- drag physics objects
- `Transform_tool` -- gizmo-based translate/rotate/scale (delegates to `Move_tool`, `Rotate_tool`, `Scale_tool`)
- `Paint_tool`, `Material_paint_tool` -- vertex/material painting
- `Hover_tool` -- displays hover information
- `Create` -- create new primitives (sphere, box, torus, cone)
- `Grid_tool` -- configurable grid display

### Rendergraph (`rendergraph/`)

Editor-specific render graph nodes:
- `Viewport_scene_view` -- renders scene content (also a `Texture_rendergraph_node`)
- `Shadow_render_node` -- renders shadow maps
- `Post_processing_node` -- bloom/tonemap post-processing

### Renderers (`renderers/`)

- `Programs` -- loads and manages all shader programs and debug visualization variants
- `Mesh_memory` -- GPU buffer allocation for vertex/index data
- `Id_renderer` -- GPU-based object picking via ID buffer
- `Composer` -- composites multiple render passes for final output

### Content Library (`content_library/`)

`Content_library` is a tree structure (`Content_library_node` extends `Hierarchy`) that organizes materials, brushes, animations, skins, and textures. Each scene has its own content library. Supports drag-and-drop in the UI.

### Windows (`windows/`)

`Dependency_property_rows` (`windows/dependency_property_rows.*`) draws the registered erhe::property properties of the inspected item(s) inside the Properties window: one widget per property type shaped by the property's `Property_ui` metadata, mixed-value display for multi-selection, "Reset to default", Copy / Paste Properties (the bag lives in `Clipboard`, with the name of the item it came from), Paste Properties as Style / Clear Style (the item's style layer, doc/erhe/property_system.md D25, through `Style_set_operation`), and undo through `Property_set_operation` / `Property_set_apply_operation` - one operation per completed drag. `App_context::on_item_property_changed` maps a property's consequence flags to editor actions (draw-list rebuild, asset dirtying) after every such write. A `Property_ui::logarithmic` slider draws with `ImGuiSliderFlags_Logarithmic` (light range / intensity, camera near / far, D20). A property driven by an expression (D22) shows its formula text in place of the widget (`=` label prefix, red frame and tooltip while it cannot be evaluated; commit on Enter or deactivation), and the context menu offers "Edit as expression" / "Remove expression". Migrated items whose rows come from here: `Material` (section 4.1), `Node` transform (4.2), `Light` (4.3, `light_properties` keeps only the derived flux, blackbody and zero-range rows; light-set re-resolve is the Light property callback, not the row) and `Camera` (4.4, `camera_properties` is gone). See doc/erhe/property_system.md D11-D12.

ImGui window implementations:
- `Viewport_window` -- hosts a `Viewport_scene_view` in an ImGui window
- `Properties` -- inspects selected item properties (camera, light, mesh, material, etc.)
- `Settings_window` -- application settings
- `Item_tree_window` -- tree view of scene hierarchy or content library
- `Viewport_config_window`, `Scene_view_config_window` -- viewport settings

### Config (`config/`)

Per-library configuration structs are loaded from individual JSON files in `config/` (e.g. `config/erhe_graphics.json`, `config/window.json`). Configuration structs are code-generated (via `erhe_codegen`) into `config/generated/`.

### Additional Subsystems

- `brushes/` -- `Brush` (parametric shape template) and `Brush_tool` (places brushes)
- `physics/` -- `Physics_tool` (drag/push/pull), `Physics_window` (settings UI), collision shape generation
- `create/` -- `Create` tool and shape generators (box, cone, torus, UV sphere)
- `parsers/` -- glTF, Wavefront OBJ, Geogram, and JSON polyhedra importers
- `assets/` -- `Asset_manager` (identity, ownership, registry: `doc/editor/asset_manager.md`) and the asynchronous load tasks (`doc/editor/async_asset_loading.md`)
- `graphics/` -- `Icon_set` (icon atlas), `Thumbnails` (material/brush previews; a slot observes the item's properties and re-renders when one changes, `doc/erhe/property_system.md` D21), gradients
- `transform/` -- Transform gizmo system (`Transform_tool` + `Move_tool`, `Rotate_tool`, `Scale_tool`)
- `xr/` -- OpenXR headset view, hand tracking, controller visualization
- `grid/` -- Grid display and snapping
- `preview/` -- Material and brush preview renderers
- `animation/` -- Animation timeline / curve graph editor (Animation_window), playback (Animation_player), keyframe edit helpers
  - Keyframing (`animation_keying.hpp`): `key_nodes()` writes the nodes' current translation / rotation / scale at a time into an animation, creating missing channels (LINEAR samplers) on demand; `delete_keys()` removes the nodes' keys at a time. Both apply immediately and return one `Animation_structure_operation` (before/after snapshot of the samplers and channels) for undo. A glTF channel keys a whole path, so keying is per path, never per axis.
  - The Animation window's timeline strip (between the transport row and the curve editor) scrubs the shared `Animation_player` time, has editable start / end time fields, and draws key markers whose source is chosen by the "Marks" combo: selected objects (default), active animation, or the channels shown in the curve editor.
  - Keying toolbar: the Autokey mode combo (`Autokey_mode` off / modified / all, held by `Animation_player`, tinted red while armed), T / R / S path toggles, "+ Key" and "- Key" for the active scene's selected nodes at the current time. "+ Key" with no targeted animation creates one in the scene's content library and targets it, in the same undo compound.
  - Autokey: `Transform_tool::record_transform_operation()` keys the edited nodes at the current time into the player's animation - only the changed paths in `modified_paths`, T, R and S in `all_paths` - and joins the keying operation to the transform's `Compound_operation`, so one undo reverts both. With no targeted animation autokey keys nothing.
- `developer/` -- Developer-only windows (clipboard, commands, composer, rendergraph, etc.)
- `experiments/` -- Experimental features (gradient editor, network, sheet)
- Mesh modeling (experimental; `tools/mesh_component_selection_tool.*`, `transform/mesh_component_transform.*`) -- the modal topology tools in a mesh component mode (loop cut): preview, topology step, chained slide, one undo entry. Described in `doc/editor/mesh_modeling.md`; component selection in `doc/editor/mesh_component_selection.md`
- Lightmap baking (experimental; `renderers/lightmap_*`) -- progressive GPU lightmap baker built on Vulkan ray query: automatic lightmap UV unwrap, world-space tile partitioning with per-tile atlases, denoise and seam handling, tiles streamed from disk. Described in `doc/editor/lightmap_baking.md`; the atlas viewer in `doc/editor/lightmap_texture_viewer.md`

## Component Construction Rules

`App_context` is a plain struct of raw pointers, all initially null. Components are constructed in dependency order by the `Editor` constructor. `App_context&` is passed to component constructors, but **component constructors must only store the reference - they must NOT access any `App_context` member pointers**. The pointers are populated later in `fill_app_context()` after all components exist.

This means:
- Constructor bodies must not read `m_context.selection`, `m_context.move_tool`, etc.
- Constructor bodies must not check `if (m_context.xxx != nullptr)` - these are always null during construction
- Cross-component wiring that requires other components to exist must happen after `fill_app_context()`, not in constructors

Components that need references to sibling components should receive them as explicit constructor parameters (not via `App_context`). For example, `Transform_tool` receives `Move_tool&`, `Rotate_tool&`, `Scale_tool&` directly - these are constructed immediately before it in the same task block.

## Important Patterns

### Dependency Injection via App_context

All subsystems receive `App_context&` and look up sibling subsystems through it. This avoids circular constructor dependencies but means subsystems are loosely typed -- null checks are important when accessing context members at runtime (after construction).

### Command System

Input handling uses `erhe::commands::Commands`. Tools register `Command` objects (e.g., `Brush_insert_command`) and bind them to keys, mouse buttons, or XR controller inputs. Commands have `try_ready()` (check preconditions) and `try_call()` (execute). `Command_host` priority determines which commands are active when multiple tools compete for the same input.

### Async Operations

Geometry operations can run asynchronously via `tf::Executor`. `async_for_nodes_with_mesh()` in `items.cpp` manages a global map of per-item async tasks, chaining dependent operations. `App_context::pending_async_ops` and `running_async_ops` are atomic counters displayed in the status bar.

The status bar (`Window_imgui_host::set_status_bar_callback`, a child of the root window under the dockspace) insets its content horizontally by one bar height, because the bottom corners of a macOS window are rounded and clip glyphs at the edges. Its frame time is `Time::get_frame_time_average_ms`, an exponential moving average of the host frame duration whose weight follows the elapsed time (`1 - exp(-dt / 0.25 s)`), so it settles within about a second at any frame rate.

### Initialization Order

Initialization is serial: `Editor::Editor()` constructs the parts in 25 named steps (`ERHE_INIT_STEP_BEGIN("<name>")` / `ERHE_INIT_STEP_END`), each a profile scope and a line on the init status display. Restoring parallel construction is planned (`doc/plans/parallel_editor_init.md`).

### Physics Integration

Each `Scene_root` owns a physics world and a `Node_physics_system`. A node carries a rigid body through the `Node_physics.*` values of the node itself (`doc/erhe/property_system.md` section 4.26); the system owns the body, the collision shape and the world registration (`doc/editor/scene.md`). Physics callbacks (`on_body_activated`/`on_body_deactivated`) set `no_transform_update` flags to let physics drive transforms for active bodies; `Node_physics_system::on_body_activation` queues the changes reported during the simulation steps (M1) and `Scene_root::apply_queued_body_activations` applies them in M2 ("Frame phases and the render thread"). The owner pointer on a rigid body names the system's `Node_physics_entry`. Dynamic physics can be toggled at runtime via `App_settings::physics.dynamic_enable`.

### Brush Placement

`Brush` is a parametric shape template (geometry + collision shape + density). `Brush::make_instance(Instance_create_info)` creates a scene node with mesh, material, and optional physics body at a pre-scaled geometry. The `place_brush_in_scene()` free function (in `brushes/brush.hpp`) wraps `make_instance()` with undo support and `set_brush_placement()`, which records the brush, facet and corner as values of the instance node itself. Both `Brush_tool` (interactive surface-aligned placement) and the MCP server (programmatic placement by position) call this shared function.

### Animation window and playback

`Animation_player` (`animation/animation_player.*`) plays one animation at a time: play / pause / stop, loop, signed speed, seek, and a user-editable timeline range that animation keys only extend. M2 of every frame advances it ("Frame phases and the render thread"), so playback does not depend on any window being visible. Applying writes the animated layer of the target properties (`doc/erhe/scene.md` "Animation playback"); `stop()` drops that layer so targets show their authored pose again. Autokey (off / modified paths / all paths) keys finished transform edits into the active animation at the current time (`animation/animation_keying.*`).

`Animation_window` (`animation/animation_window.*`) is the UI in front of the player: an animation combo, transport and keying toolbars (Create Key / Delete Key for the selected nodes with T / R / S toggles; Create Key makes an animation when none is active), a timeline strip that scrubs and shows key markers (of selected objects, the whole animation, or the shown channels), a channel list (name filter, per-component visibility, All / None / Animated, context menu selecting the target node), and a curve canvas. The canvas pans (MMB / RMB drag), zooms around the cursor (wheel; Ctrl zooms time only, Alt value only), frames all (Home), selects keys (click, Ctrl-click toggle, box), drags keys in time and value, inserts a key with Ctrl+click near a curve and deletes selected keys with Delete / X. Each edit is one undo entry (`operations/animation_edit_operation.*`); edits go through the helpers in `animation/animation_edit.hpp`. The Properties window's animation section has an "Open in Animation Window" button. MCP tools: `get_scene_animations`, `set_animation_target`, `animation_playback`, `animation_edit_keyframe`, `animation_create_key`, `animation_delete_key`. Remaining work: `doc/plans/timeline_editor.md` and `doc/plans/animation_keyframing.md`.

### Scene Serialization

Scenes persist as single erhe-authored glTF files (`.glb`; `ERHE_scene` in `extensionsUsed` marks the file): one `export_gltf()` call carries render content, physics (KHR_physics_rigid_bodies), prefab external-asset references, texture sources, animations, and the editor-domain `ERHE_*` extension payloads (`parsers/gltf.hpp` `save_scene_gltf` / `open_scene_gltf`; full reference `doc/editor/scene_serialization.md`, design record `doc/editor/gltf_scene_roundtrip.md`). Collision shape types (box, sphere, cylinder, capsule, compound) are persisted and recreated faithfully on load rather than degrading to convex hulls.

### Asynchronous asset loading

A glTF load is an `Asset_load_task` owned by `Asset_manager` and advanced a
bounded amount from `Asset_manager::tick()`, called in M2 ("Frame phases and
the render thread") right after `Scene_commit_queue::flush()`. Scan, parse and `Buffer_mesh` build run on
workers; texture residency is spread over frames under `Frame_load_budget`;
vertex/index bytes go through a second, budget-drained `Mesh_memory` transfer
queue and publish is gated on its watermark. Converted entry points: erhe-scene
open, foreign-glTF open, import-into-scene, and prefab template load
(`assets/asset_load_task.hpp`, `assets/gltf_load_task.hpp`). Master switch
`Load_config::async_gltf_load`, default on; with it off every path falls back
to the original blocking code, which is kept for comparison. Full reference:
`doc/editor/async_asset_loading.md`, design history `doc/editor/async_asset_loading_design.md`.

`Asset_manager::get_or_load_container()` and `Prefab_library`'s nested
external-asset resolution are the parts that are still synchronous.

### Item Locking and Tagging

Items have three lock flags (in `Item_flags`):
- `lock_viewport_selection` (bit 7) - prevents viewport click-to-select
- `lock_viewport_transform` (bit 8) - prevents viewport gizmo transform
- `lock_edit` (bit 25) - prevents deletion and property editing; `Selection::delete_selection()` skips locked items; Properties window disables editing when set

Items also support arbitrary string tags via `add_tag()`, `remove_tag()`, `has_tag()`, `get_tags()`. Tags and lock state are exposed in MCP node query responses and can be set via `lock_items`, `unlock_items`, `add_tags`, `remove_tags` MCP tools. The Properties window shows lock toggles at the top of each item.

### Content Library Drag-and-Drop

Content library items can be dragged between libraries (e.g., materials from one scene to another). Cross-library drops perform a copy via `Material` copy constructor + `Item_insert_remove_operation`. Same-library drags are ignored. Materials can also be dropped onto scene nodes to assign them to mesh primitives.

### MCP Server (`mcp/`)

The editor embeds an MCP (Model Context Protocol) server on `127.0.0.1:3743` for external tool integration. It exposes:

- **Query tools**: `list_scenes`, `get_scene_nodes`, `get_node_details`, `get_scene_cameras`, `get_scene_lights`, `get_scene_materials`, `get_material_details`, `get_scene_brushes`, `get_brush_geometry_states`, `get_selection`, `get_physics_items`
- **Action tools**: `select_items` (by ID), `place_brush` (by brush ID + position), `request_brush_geometry` (tier 2 brush geometry preparation, doc/editor/brushes.md G3), `toggle_physics`, `lock_items`, `unlock_items`, `add_tags`, `remove_tags`
- **Physics tools**: `create_physics_body` / `edit_physics_body` (the `Node_physics.*` values of a node), `create_joint` / `edit_joint` (the `Joint` prim below a node), `create_physics_material` / `edit_physics_material`, `create_collision_filter` / `edit_collision_filter`, `create_physics_joint_settings` / `edit_physics_joint_settings` (shared content-library items), `wake_physics_bodies`; `get_node_details` reports the body under `physics`
- **Property tools**: `get_item_properties` / `set_item_property` - the generic erhe::property view of any item (registered properties with value, source, default, enum labels, expression text and error; undoable writes of a value or an `expression` formula through `Property_set_operation`; an object property (a material texture slot, a primitive's material) is written by name or `reference_id`, and `sub_object` addresses a property sub-object such as a mesh primitive listed under `sub_objects` - doc/erhe/property_system.md D28, D29; the item reports `sealed` and `style`), `set_item_style` / `clear_item_style` (the style layer, D25, through `Style_set_operation`). See doc/erhe/property_system.md D13, D22, D25.
- **Animation tools**: `animation_playback` (transport, target, `autokey` mode `off` / `modified` / `all`), `animation_edit_keyframe`, `animation_create_key` / `animation_delete_key` (key or unkey nodes at a time on chosen paths through `key_nodes()` / `delete_keys()`; create-key creates and targets an animation when none is targeted)
- **Editor commands**: All registered `Command` objects (undo, redo, delete, etc.)

The HTTP server (cpp-httplib) runs on a background thread. All requests are queued to the main thread via `std::promise`/`std::future` for thread safety. `process_queued_requests()` is called once per frame in M2 ("Frame phases and the render thread"). `GET /health` answers 503 until that first call and 200 from then on, so a client that waits for 200 never sends a `tools/call` the main loop cannot answer within the 5 s request timeout. The listening socket sets `SO_EXCLUSIVEADDRUSE` (Windows) / `SO_REUSEADDR` only (POSIX) so a second editor's bind on the port fails and its port scan moves on, instead of two editors sharing one port. See `doc/agents/mcp_server_usage.md` for full API reference with curl examples.

## Dependencies

- erhe libraries: commands, graphics, imgui, renderer, rendergraph, scene, scene_renderer, physics, raytrace, geometry, primitive, item, window, log, verify, configuration, file, gltf, math, time, profile, message_bus, graph, ui, xr
- External: Taskflow, ImGui, GLM, Geogram, spdlog/fmt, simdjson, Jolt (physics), SDL/GLFW, OpenXR, cpp-httplib, nlohmann/json

## Future work

- Parallel part construction: `doc/plans/parallel_editor_init.md`
- Keyframing and timeline: `doc/plans/animation_keyframing.md`
- Dope sheet and curve editor: `doc/plans/timeline_editor.md`
- Render thread: headset verification, upload job queue and OpenGL, Metal audit: `doc/editor/render_thread.md` "Future work"
