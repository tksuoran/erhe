# Changelog

All notable changes to the public API of the erhe libraries (`erhe::*`,
`src/erhe/`) are recorded here. The editor and the other executables are not
covered. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the rule for adding entries is in `doc/README.md` ("Changelog").

## [Unreleased]

### Added

- `erhe::frame_pacing`: `Slop_servo_pacer::get_cadence()` and
  `get_cadence_period()` (the loop period the servo paces against: the
  tracked plant loop period, at least one refresh period) and
  `Slop_servo_tunables::cadence_smoothing`. The overshoot reference and the
  sleep clamp are the cadence period instead of one refresh period, so the
  servo also paces loads heavier than a refresh period;
  `set_refresh_period()` restarts the cadence estimate.

- `erhe::primitive`: `optimize_indexed_mesh_cached(streams, indices,
  vertex_format, options, cache_directory, result)`, the filesystem-cached
  form of `optimize_indexed_mesh()` (a hit replays the stored remaps into the
  same streams, indices and remaps without running meshoptimizer), and
  `Buffer_info::mesh_optimize_cache_directory` (empty by default), through
  which the geometry-path optimized variant of
  `make_optimized_render_shape_from_staged_build()` is cached.
  `optimize_triangle_soup_cached()` now uses the same cache; its entries
  are format version 3, so older entries miss once and are rewritten.

- `erhe::graphics`: `Render_command_encoder::multi_draw_indexed_primitives_indirect_count(
  primitive_type, index_type, indirect_offset, count_buffer, count_offset,
  max_draw_count, stride)` (the multi-draw with a GPU-read draw count: Vulkan
  `vkCmdDrawIndexedIndirectCount`, GL `glMultiDrawElementsIndirectCount`;
  fatal on Metal and the null device) and `Device_info::use_draw_indirect_count`
  (Vulkan: `VK_KHR_draw_indirect_count` enabled on a device with native
  multi-draw indirect; GL 4.6; false on Metal and the null device).

- `erhe::scene_renderer`: GPU command generation of the shadow passes
  (`doc/plans/culling.md` phase 6). `Shadow_command_selection` and
  `Draw_list_scene::set_shadow_command_selection(frame_index, selection)`
  (`Shadow_renderer::fit()` calls it), with which a frame whose slot records
  `Cull_classification::gpu` generates the shadow passes' commands on the GPU
  (`res/shaders/cull_compact.comp`) and `draw_shadow()` draws them with the
  count draw; `Gpu_cull_classifier` constants and accessors of the compaction
  (`compact_*_binding_point`, `compact_workgroup_size`,
  `template_bytes_per_entry`, `job_bytes`, `job_command_alignment`,
  `count_bytes_per_job`, `get_compact_bind_group_layout()`,
  `get_compact_bits_pipeline()`, `get_compact_pipeline()`,
  `get_job_buffer()`, `get_bits_buffer()`, `get_command_buffer()`,
  `get_count_buffer()`, `get_job_*_offset()`);
  `Shadow_command_selection` carries the drawing `Primitive_buffer`, whose
  record capacity (`Draw_list_scene::get_records_per_chunk()`) both the job
  table and the draws cut chunks with; a GPU-command pass counts no entries
  on the CPU: `Draw_list_scene::has_gpu_shadow_commands(frame_index)`,
  `get_retired_gpu_shadow_counts()` (`Retired_gpu_shadow_counts`,
  `Gpu_shadow_view_counts`: per view drawn / filtered of the newest retired
  frame) and `Shadow_draw_statistics::gpu_counts_frame_index` (the entry and
  culled counts are that retired frame's, frames in flight late);
  `Draw_indirect_buffer::is_index_count_clamped()` (Shadow_renderer keeps the
  CPU commands while the debug clamp is on); `Gpu_cull_classifier::split_uint64()`;
  `Cull_classification_check` reports the command generation (`job_count`,
  `command_count`, `command_mismatch_count`, `command_mismatches`
  (`Cull_command_job_mismatch`), `command_views` (`Cull_command_view_check`),
  `command_gpu_time_ns`).

- `erhe::rendergraph`: `Rendergraph_node::pre_execute_rendergraph_node(command_buffer)`
  (virtual, default nothing) and `Rendergraph::set_pre_execute_callback(callback)`:
  `Rendergraph::execute()` runs every enabled node's hook in execution order,
  then the callback, before the first `execute_rendergraph_node()`
  (`doc/erhe/rendergraph.md` "Pre-execute hook").

- `erhe::scene_renderer`: `Draw_list_scene::classify_frame_cull_views(frame_index,
  command_buffer)` (the frame's one classification of every view, CPU and GPU;
  `doc/plans/culling.md` phase 6a), `Cull_frame_registry`
  (`cull_frame_registry.hpp`: `add_frame_scene()`, `classify_frame(command_buffer)`,
  which classifies every scene that added views this frame) and the required
  `Draw_list_scene_create_info::cull_frame_registry`,
  `Shadow_renderer::fit(parameters, cull_views)` /
  `draw(parameters, cull_views)` with `Shadow_renderer::Cull_views`,
  `Draw_list_renderer::add_color_cull_view(draw_list_scene, frame_index,
  Color_cull_view_parameters)` and `Draw_list_renderer::Render_parameters::cull_view`.

- `erhe::scene_renderer`: the GPU culling classification
  (`doc/plans/culling.md` phase 5). `Cull_classification` (`cpu`, `gpu`,
  `count`) and `c_str(Cull_classification)` (`cull_classification.hpp`);
  `Gpu_cull_classifier` (`gpu_cull_classifier.hpp`:
  the device-wide compute pipeline of `res/shaders/cull_classify.comp`,
  `is_device_capable()`, `is_supported()`, `record_pending(command_buffer)`,
  which the application calls in the render phase after its last pass that
  draws draw lists); `Draw_list_scene_create_info::gpu_cull_classifier`;
  `Draw_list_scene::record_cull_classification(frame_index, command_buffer)`
  (classifies the frame's views added so far and records their GPU dispatch,
  outside any render pass; `Shadow_renderer::render()` calls it before its
  first pass), `get_cull_classification()`, `get_gpu_bounds_write_count()`,
  `request_cull_classification_check()` /
  `poll_cull_classification_check(out)` with `Cull_classification_check`,
  `Cull_classification_list_check`, `Cull_classification_mismatch` and
  `Cull_classification_check_state`.

- `erhe::graphics`: `Record_indexing` (`draw_position`, `first_instance`),
  `Device_info::record_indexing` and `c_str(Record_indexing)`: how a draw
  names its record. The GLSL preamble defines `ERHE_PRIMITIVE_INDEX`, the
  record index (`gl_InstanceIndex` on Vulkan, `gl_BaseInstance` /
  `gl_BaseInstanceARB` + `gl_InstanceID` on GL, with `first_instance`;
  `ERHE_DRAW_ID` otherwise and on Metal).
  `erhe::scene_renderer`: `Primitive_interface::record_indexing`;
  `Primitive_buffer::update_records(draw_list, begin, end, settings)` (the
  records of every entry of a chunk); `Draw_record_layout` (`packed`,
  `chunk_entries`); `Draw_list_scene::begin_shadow_record_scope()` and the
  move-only `Draw_list_scene::Shadow_record_scope`, whose destructor releases
  the shared shadow records (`Shadow_renderer::render()` holds one across its
  passes).

- `erhe::math`: `calculate_bounding_convex_hull_of_boxes()` and
  `Box_hull_scratch` (`box_hull.hpp`): the 2D convex hull of many projected
  `Aabb`s, identical to hulling every corner, with interior boxes pruned
  before the sort. `erhe::scene`: `Shadow_fit_scratch` gains
  `interior_caster_aabbs`, `boundary_points`, `box_hull` and the `Abs_plane`
  vectors `filter_abs_planes`, `receiver_abs_planes` and `volume_abs_planes`
  (planes with their absolute normals, for the center + extents caster
  tests); `surviving_caster_aabbs` is removed (the caster cull and the per-box
  clip are one pass) and `caster_points` is filled only when `collect_debug`
  is on.

- `erhe::graphics`: `Command_buffer::get_active_render_pass()` /
  `set_active_render_pass()` (set by `Render_pass::start_render_pass()` /
  `end_render_pass()`); `Ring_buffer::try_acquire()` and `Ring_buffer_wrap`
  (check the space and acquire under one lock).

- `erhe::rendergraph`: virtual `Rendergraph_node::on_released()`, called by
  `Rendergraph_node_deleter` when the last owner releases the node, before
  the rendergraph unregisters and retires it. `erhe::imgui`: `Imgui_host`
  overrides it and leaves the `Imgui_renderer`'s host list at the release
  instead of at its destruction.

- `erhe::rendergraph`: `make_rendergraph_node<T>(args...)` and
  `Rendergraph_node_deleter`: every node is created as a `std::shared_ptr`
  whose deleter hands it back to the rendergraph, which unregisters it at
  once and destroys it once no frame still to be rendered names it;
  `Rendergraph::end_rendering()` (shutdown, after the last render phase)
  destroys the retired nodes.

- `erhe::rendergraph`: `Rendergraph::prepare_frame()` (main thread, prepare
  phase) calls each enabled node's new virtual
  `Rendergraph_node::prepare_rendergraph_node(frame_index)` hook, then copies
  every node's enabled flag into the frame (one copy per frame in flight);
  `Rendergraph_node::is_enabled_in_frame(frame_index)`. Programs without a
  separate prepare phase call `prepare_frame()` before `execute()`.

- `erhe::scene_renderer`: `Content_wide_line_tent_settings` (`use_tent`,
  `line_bias_margin`, `line_bias_clamp`), passed per frame to
  `Content_wide_line_renderer::set_view_params()`.

- `erhe::imgui`: per-frame draw data copies. `Imgui_draw_data_copy` /
  `Imgui_draw_list_range` (one context's `ImDrawData` deep-copied into owned
  vectors), `Imgui_host::copy_draw_data()` (ImGui phase, called by
  `Imgui_windows::end_frame()`) and `Imgui_host::get_draw_data()` (render
  thread, the slot of its device frame), `Imgui_renderer::copy_draw_data()`,
  `get_imgui_frame_index()`, `get_render_frame_index()`,
  `get_number_of_frames_in_flight()`; `Imgui_texture` and
  `Imgui_texture_request` (ImGui texture requests queued by the ImGui phase,
  run by `update_draw_data_textures()` on the render thread).

- `erhe::graphics`: `Multi_copy_buffer::publish()` names the current copy as
  the copy the caller's device frame (the role-keyed
  `Device::get_frame_index()`) binds and stamps it as used by that frame;
  `commit()` publishes the copy it commits.

- `erhe::rendergraph`: `Texture_rendergraph_node::request_size(width,
  height)` (main thread, prepare phase; one request per frame in flight) and
  `apply_requested_size()` (render phase, at the start of the node's
  execute), so the render target is resized by the render phase only;
  `Render_target::get_sample_count()`.

- `erhe::scene_renderer`: `Content_wide_line_record` and
  `Content_wide_line_feed` (`content_wide_line_feed.hpp`), one view's
  prepared edge lines; `Content_wide_line_renderer::prepare_mesh(...,
  Content_wide_line_feed&)` (static, reads the mesh) and `add_feed(feed)`
  (queues the dispatches, reads no mesh). `add_mesh()` does both.

- `erhe::scene_renderer`: `Shadow_renderer::prepare(Prepare_parameters,
  Render_bucket_snapshot&)`, the bucket path's casters prepared without the
  pass variant bits; `Shadow_renderer::Render_parameters::casters` (draw a
  prepared snapshot, ORing the depth-only / distance / cube bits per pass)
  and `shadow_bounds` (the frustum fit bounds, e.g. a
  `Draw_list_scene::get_shadow_bounds` slot, on either path).

- `erhe::scene_renderer`: `Primitive_buffer::append_records(bucket,
  material_source, mode, settings, io_id_offset, id_ranges, out)`, the id
  pass's prepared records with their `Id_range` table;
  `Primitive_buffer::first_id_offset` and `get_record_size()`.

- `erhe::scene_renderer`: `Render_bucket_snapshot` and `Prepared_bucket`
  (`render_bucket_snapshot.hpp`), one pass's prepared bucket-path draws
  (buckets, complete primitive records, draw commands).
  `Forward_renderer::prepare(Prepare_parameters, Render_bucket_snapshot&)`
  buckets and writes them; `Forward_renderer::render(Draw_parameters, const
  Render_bucket_snapshot&)` draws them without reading any mesh, applying
  only the render-phase environment shader-key axes.
  `Primitive_buffer::append_records(...)` / `update(std::span<const
  std::byte>)` and `Draw_indirect_buffer::append_commands(...)` /
  `update(std::span<const Draw_indexed_primitives_indirect_command>)`, the
  prepare and draw halves of the record and command writes.

- `erhe::scene_renderer`: `Draw_list_scene::publish(frame_index,
  frames_in_flight)` (main thread, prepare phase, after `flush_pending()` and
  after the frame's `Joint_poses::capture`) rewrites the joint slot fields of
  the skinned objects whose skin moved to another slot and copies every draw
  list that changed since the frame-in-flight slot's last publish into that
  slot; `get_published_list_copy_count()` counts the copies.
  `Draw_list::identity` and `Draw_list::serial`; `Draw_list_resolutions`, the
  render phase's resolved-stages cache of one list.
- `erhe::scene_renderer`: `Draw_list_scene::prepare_shadow_bounds(
  Shadow_bounds_selection, frame_index, frames_in_flight)` (main thread,
  prepare phase) gathers the shadow frustum fit's caster and receiver bounds
  and caster vertex extents into one `Shadow_bounds` slot per frame in
  flight, again only when the records changed (the skinned objects' posed
  bounds are rewritten every call); `get_shadow_bounds(frame_index)`
  (render phase) returns the frame's slot; `get_shadow_bounds_gather_count()`
  counts the full gathers. `shadow_caster_filter`, the shadow pass filter.
- `erhe::graphics`: `Device::end_frame(uint64_t frame_id)` ends the oldest
  open device frame and verifies `frame_id` is that frame (`end_frame()`
  ends the oldest open frame); `Command_buffer::get_frame_id()`, the device
  frame a command buffer was obtained for.
- `erhe::utility`: `Thread_roles::is_render_role()`, true on a dedicated
  render thread.
- `erhe::utility`: `Frame_phase::m3_imgui` (the main thread's ImGui phase,
  which may run while the render thread renders the same frame) and
  `Thread_roles::assert_main_thread_in_phase(Frame_phase, Frame_phase)`,
  the either-of-two-phases assert; `assert_command_recording_allowed` now
  refuses the main thread in `m3_imgui` too.
- `erhe::imgui`: `Imgui_renderer::set_draw_data_wait(std::function<void()>)`
  and `wait_for_draw_data()`: the ImGui host rendergraph nodes call the
  latter before they read the frame's draw data, so an application whose
  ImGui phase runs concurrently with its render phase can block them until
  the draw data is published. `Window_imgui_host::update_render_pass(int,
  int)` is public: the application (re)creates the host's swapchain render
  pass on the thread that owns the swapchain.
- `erhe::scene`: `Light_snapshot` (`erhe_scene/light_snapshot.hpp`), the
  shading parameters of a `Light` copied for a render phase: the effective
  property values, the light frame, id and name, with `is_active()`,
  `casts_shadow()`, `projection()` and `projection_transforms()` (the
  projection fits now compute on the copy). `Light_projection_transforms::light`
  points at the snapshot.
- `erhe::scene_renderer`: `Joint_pose` and `Joint_poses`
  (`erhe_scene_renderer/joint_poses.hpp`): the posed joints of a scene's skins
  copied for a render phase (`capture(skins)` on the owning thread writes
  `Skin_data::joint_buffer_index` and computes `world_from_bind` and the
  normal transform per joint slot).
- `erhe::scene_renderer`: `Light_set::find_slot(std::size_t light_id)` and
  `Light_projections::get_light_projection_transforms_for_light_id(std::size_t)`,
  the lookups by light id.
- `erhe::scene`: `Camera_snapshot` (`erhe_scene/camera_snapshot.hpp`), the view
  parameters of a `Camera` copied for a render phase: projection, world
  transform, exposure, shadow range, with `projection_transforms()` and the
  transform accessors of the node. The renderers read the copy, so the camera
  may be edited on another thread while a frame renders.
- `erhe::graphics`: `Ring_buffer_range::is_released()`.
- `erhe::graphics`: `Device::wait_for_frame_start() -> uint64_t` and
  `Device::acquire_frame_slot(uint64_t frame_id) -> bool`, the two halves of
  `Device::wait_frame()` (which now calls both). `wait_for_frame_start` opens
  the frame time record and refreshes the GPU calibration and frame bracket
  results without reading the frame index, so it may run while another
  thread ends the previous frame; `acquire_frame_slot` verifies the started
  id is the current frame index and does the timeline wait and pool resets.
- `erhe::frame_pacing`: `Frame_time_record::present_wait_begin` /
  `present_wait_end` (the FR5 present-wait clamp span) and
  `join_wait_begin` / `join_wait_end` (an application's wait for its
  previous frame's render phase inside the CPU slot), both subtracted from
  `cpu_service_time()` like the pacer wait.
- `erhe::xr`: `Xr_session::set_command_buffer_thread_slot(unsigned int)`, the
  device thread slot `render_frame()` / `render_frame_multiview()` take their
  view command buffers from (default 0, the previous fixed slot).
- `erhe::scene_renderer`: `Draw_list_scene_create_info::exclude_unlit_from_shadows`,
  the initial value of `Draw_list_scene::set_exclude_unlit_from_shadows()`.
- `erhe::profile`: `ERHE_PROFILE_THREAD_NAME(name)` names the calling thread
  in the profiler (Tracy `tracy::SetThreadName`; no-op for the other back
  ends).
- `erhe::utility`: `Thread_roles` (`erhe_utility/thread_roles.hpp`) and
  `Frame_phase`: process-wide main / render thread ids and the current frame
  phase, with `assert_main_thread()`, `assert_render_thread()`,
  `assert_frame_phase()`, `assert_main_thread_in_phase()` and
  `assert_command_recording_allowed()`; every assert is a no-op until the
  application sets the roles. `Rendergraph::execute`,
  `Draw_list_scene::draw_color` / `draw_shadow` / `gather_shadow_bounds`
  (previously main-thread asserts), `Imgui_renderer::render_draw_data` and
  `Xr_session::render_frame*` / `end_frame` assert the render role and phase;
  `Draw_list_scene::flush_pending`, `Imgui_windows::begin_frame` and
  `Xr_session::begin_frame` / `wait_frame` assert the main role;
  `Device::get_command_buffer` refuses the main thread during `m1_overlap`.
  `erhe_utility` now links `erhe::verify`.
- `erhe::graphics`: `Graphics_config::vulkan.vulkan_gpu_assisted_validation`
  (config version 2), GPU-assisted validation in place of synchronization
  validation; debug-level resource and address traces (`erhe.graphics.texture`,
  `erhe.graphics.buffer`, `erhe.graphics.render_pass`, `erhe.graphics.debug`
  `[VA]` through `VK_EXT_device_address_binding_report`), see
  `doc/erhe/vulkan_backend.md` "Logging and debugging".

- `erhe::version` (`erhe_version/version.hpp`): `get_project_version()`,
  `get_git_describe()`, `get_description()`, the build identity stamped
  into one translation unit at every build (`erhe_version_stamp`).
- CMake: `ERHE_USE_UBSAN` (UndefinedBehaviorSanitizer on Clang, GCC and
  AppleClang) beside `ERHE_USE_ASAN`, and `ERHE_WARNINGS_AS_ERRORS` (default
  on): every toolchain file applies erhe's warning set and `-Werror` /
  `/WX` per erhe target. CI runs a Clang ASan+UBSan matrix entry.
- `erhe::scene_renderer`: `Draw_list_scene::gather_shadow_bounds()` with
  `Shadow_bounds_gather_parameters`: the shadow frustum fit's caster /
  receiver bounds and caster vertex extents from the registered objects
  (`Draw_list_object::node_abs_extent`). `Draw_shadow_parameters::
  light_frustum_planes`: shadow passes cull entries against the light
  frustum. `Shadow_draw_statistics` and `Shadow_renderer::Render_parameters::
  draw_statistics`: per shadow pass draw-list counts.
  `Draw_list_scene::enqueue_bounds_update()`: rewrite an object's entry
  AABBs and node extent at the next flush.
- `erhe::scene`: `Mesh::notify_primitive_bounds_changed()` and the new
  `Scene_host::on_mesh_bounds_changed()` virtual: a primitive's bounds grew
  in place (the mesh-component drag); the host refreshes the bounds of
  every mesh naming the primitive.

- `erhe::item`: `erhe_item/item_flags.hpp` (`Item_flags`, `Item_filter`,
  `Item_flag_info`) and `erhe_item/item_type.hpp` (`Item_type`,
  `Item_type_info`), split out of `item.hpp`, which includes both. The
  application ranges: `Item_flags::application_bit(i)` (bits 32 to 63) with
  `register_application_flags()`, `get_application_flags()`, `label()`,
  `get_transient_bits()`, `get_purpose_guide_when_set_bits()`,
  `get_purpose_inputs()`; `Item_type::application_index(i)` /
  `application_bit(i)` (indices 1 to 31, the library types from
  `library_first_index` 32) with `register_application_types()`,
  `get_application_types()`, `label()`, `index_count`. The capability
  types `Item_type::texture_reference` and `style_source`.
  `Item_base::register_flag_bit_property()` is public.
- `erhe::profile`: `erhe_profile/profile_mutex.hpp`, the profiler-aware
  mutex macros alone (`profile.hpp` includes it).
- `erhe::gltf`: `Gltf_export_arguments::excluded_item_flag_bits` /
  `excluded_item_type_bits`; `erhe::usd`: the same on `Usd_save_arguments`.
- `erhe::scene_renderer`: `Primitive_interface_settings::hovered_flag_bits` /
  `active_item_flag_bits`.
- `erhe::scene_renderer`: `Cull_sphere` (`draw_list.hpp`) and
  `Draw_list_scene::add_point_light_cull_views(frame_index, purpose,
  range_sphere)`: the six cube face views of a point light (range sphere,
  then the face's octant). `Draw_list_scene::has_cull_view_survivors()`
  (does a view keep any entry) and `get_filtered_entry_count()`.
  `Shadow_draw_statistics` gains `point_cube_faces` (per face counts) and
  `point_cube_skipped_faces` (per cube, the faces that drew nothing).
  `Draw_list_bounds::is_bounded()` is replaced by the static
  `get_unbounded_flag(extent_x)`.

### Changed

- `erhe::scene_renderer`: `Gpu_cull_classifier::is_device_capable()` also
  needs `erhe::graphics::Device_info::use_draw_indirect_count` (and six
  compute storage blocks), and `is_supported()` the two compaction
  pipelines; under `Cull_classification::gpu` the shadow passes of a frame
  with a `Shadow_command_selection` draw GPU-generated commands
  (`doc/plans/culling.md` phase 6; the color passes keep the CPU-written
  commands).

- `erhe::scene_renderer`: every culling view of a frame is added before the
  frame's classification (`Draw_list_scene::classify_frame_cull_views()`);
  `add_cull_view()` after it, and `draw_color()` / `draw_shadow()` /
  `has_cull_view_survivors()` naming a view not classified, are verified
  defects. `Draw_list_renderer::render()` no longer adds the color view (the
  caller passes `Render_parameters::cull_view`); `Shadow_renderer::render()`
  takes the bucket path only (`draw_list_scene` null, verified; the
  draw-list path is `fit()`, the frame's classification, `draw()`). The GPU
  classification is one dispatch per scene per frame with one mask region
  (`group * entry_count + entry`); `Cull_classification_check::gpu_time_ns`
  is one value and `dispatch_count` is 1 (0 without entries).

- `erhe::scene_renderer`: `Draw_list_scene::publish(frame_index,
  frames_in_flight, classification)` takes the frame's
  `Cull_classification`; with `gpu` (on a supported device) it also writes
  the bounds of the lists whose slot copy changed into the slot's GPU bounds
  buffer.

- `erhe::scene_renderer`: the shaders index `primitive.primitives[]` with
  `ERHE_PRIMITIVE_INDEX` instead of `ERHE_DRAW_ID`, and every
  `Draw_indirect_buffer` writer sets a command's `base_instance` to its record
  index (the command's position in its multi-draw) under
  `Record_indexing::first_instance`, 0 otherwise.
  `Draw_indirect_buffer::update(draw_list, begin, end, entry_passes)` takes a
  `Draw_record_layout`. With `first_instance`, `Draw_list_scene::draw_shadow()`
  uploads each shadow list chunk's records once per `Shadow_record_scope`
  and the passes write only their commands.
  `Draw_indirect_buffer::update(span<commands>)` writes each command's
  `first_instance` and verifies the commands carry 0.

- `erhe::scene_renderer`: draw-list culling runs through a per-frame view
  set: `Draw_list_scene::add_cull_view(frame_index, purpose, planes)` returns
  a `Cull_view` (`draw_list.hpp`; `Cull_view::none` draws unculled), and
  `Draw_color_parameters::cull_view` / `Draw_shadow_parameters::cull_view`
  replace `view_frustum_planes` / `light_frustum_planes`. `Draw_list` gains
  `bounds` (`Draw_list_bounds`: the entries' world AABB centers and half
  extents as six float arrays, parallel to `entries`).

- `erhe::graphics`: `max_texture_heap_size` moved from the Vulkan backend
  header to `texture_heap.hpp` (public, 4096; 1024 on Android) and is the
  default `Texture_heap` `max_textures`.

- `erhe::graphics`: the Vulkan device is safe for the main role preparing
  frame N+1 while the render role renders frame N (`doc/erhe/graphics.md`
  "Thread safety"): `Device::get_frame_index()`, `is_frame_completed()`,
  `add_completion_handler()`, `allocate_ring_buffer_entry()`,
  `get_command_buffer()` and `submit_command_buffers()` may be called from
  either role; `acquire_frame_slot()` asserts the main thread and
  `begin_frame()` / `end_frame()` / `set_present_target_time()` the render
  thread (`erhe::utility::Thread_roles`, no-ops while the roles are unset).
  `Device::wait_idle()` completes the frames that ended before its device
  wait and keeps the completion handlers of a frame that is still open
  (they ran at once before); with no frame open it still drains every
  handler. The fallback descriptor pool (no push descriptors) is one pool
  per frame in flight, reset by `acquire_frame_slot()`.

- `erhe::graphics`: `Ring_buffer_pool` and `Ring_buffer` are thread-safe
  (a mutex in each), and `Ring_buffer_pool` keeps its ring buffers per
  thread role, so the main role's and the render role's ranges never
  interleave in one buffer (`doc/erhe/ring_buffer_memory.md` "Roles and
  threads").

- `erhe::graphics`: destroying a Vulkan `Gpu_timer` releases its query slot
  only when the frame current at the destruction completes; no result is
  stored into the timer after its destructor started.

- `erhe::rendergraph`: `Rendergraph::prepare_frame()` sorts the graph and
  copies the node list into the frame's slot and each node's connections
  into its record of the frame; `execute()` runs the frame's slot without
  the graph mutex (and verifies the slot's frame). In the render phase
  `Rendergraph_node::get_consumer_input_node()` /
  `get_producer_output_node()` answer from the frame's record. `connect()` /
  `disconnect()` / `register_node()` are main thread only and take the graph
  mutex. `~Rendergraph_node()` verifies the node is unregistered.

- `erhe::rendergraph`: `Rendergraph::execute()` runs the nodes enabled in
  the copy `prepare_frame()` made for the frame it renders
  (`is_enabled_in_frame`), no longer the live `is_enabled()` flag; a node
  registered after that copy takes part from the next frame.

- `erhe::scene_renderer`: `Content_wide_line_renderer::set_view_params()`
  takes the frame's `Content_wide_line_tent_settings` as its second
  argument.

- `erhe::renderer`: `Debug_renderer::get_line_bias_margin()` /
  `get_anti_aliasing()` return the calling role's device frame copy, taken
  from the live values (`set_line_bias_margin` / `set_anti_aliasing`) at the
  frame's first `begin_frame()`; a setting change applies from the next
  device frame recorded after it.

- `erhe::imgui`: `Imgui_renderer::render_draw_data(encoder, render_pass,
  draw_data, transform)` takes the host's `Imgui_draw_data_copy`
  (`Imgui_host::get_draw_data()`) instead of reading the current context's
  `ImGui::GetDrawData()`; `update_draw_data_textures()` runs the queued
  texture requests and needs no current ImGui context; `image()` /
  `image_button()` keep their texture references per frame in flight;
  `Imgui_renderer::begin_frame()` asserts the ImGui phase.

- `erhe::graphics`: `Multi_copy_buffer::bind()` binds the copy published for
  the caller's device frame and writes nothing (it verifies the frame was
  published); copies are stamped as used at `publish()` / `commit()` instead
  of at `bind()`. An owner calls `publish()` (or `commit()`) once per frame
  before the frame's first `bind()`, so the thread recording frame F may
  bind while the thread preparing frame F + 1 writes.

- `erhe::scene_renderer`: `Material_set::update()` runs once per frame before
  the frame's first `bind()`: every call, written or clean, publishes the
  buffer copy and texture heap that frame binds, and each write goes to a
  texture heap no unretired frame was published with (the set keeps a pool
  of heaps). `bind()` / `unbind()` read only what was published for the
  caller's frame.

- `erhe::rendergraph`: `Rendergraph::defer_resource()` is render phase only
  (verified with `erhe::utility::Thread_roles`).

- `erhe::renderer`: `Debug_renderer` and `Text_renderer` keep their
  recording state (frame slots, view stack, bucket draws, prints) once per
  device frame in flight and key every entry point by the role-keyed
  `Device::get_frame_index()`; slot ids count from 0 within each device
  frame, and `compute()`, `render()` and `release_frames()` touch only the
  calling role's frame (they assert the render role and phase).
  `push_view`, `pop_view`, `get_view`, `get_frame_slot`,
  `is_multiview_active` and `get_multiview_views` are no longer inline.
  `Primitive_renderer`'s constructor takes the device frame index
  (`Debug_renderer::get()` passes it); `Debug_renderer_bucket::start_view`,
  `make_draw`, `dispatch_compute`, `render` and `release_buffers` take it,
  the single-`View` `start_view` overload and `clear()` are removed.

- `erhe::scene_renderer`: the debug joint colors are read-only views:
  `Joint_buffer::update`, `Scene_pass_resources::begin_pass`,
  `Forward_renderer::Render_parameters` / `Draw_parameters` and
  `Draw_list_renderer::Render_parameters` take `debug_joint_colors` as
  `std::span<const glm::vec4>` (was `std::span<glm::vec4>`).

- `erhe::scene_renderer`: `Content_wide_line_renderer`'s subclass hook is
  `add_record(const Content_wide_line_record&)` instead of
  `add_primitive(mesh_memory, buffer_mesh, ...)`; the base resolves the
  edge-line and joint buffers when it prepares the record.

- `erhe::scene_renderer`: `Draw_list_scene::draw_color()`, `draw_shadow()` and
  `has_drawable_entries()` read the lists `publish()` published for the frame
  (`Draw_color_parameters::frame_index`, `Draw_shadow_parameters::frame_index`,
  `has_drawable_entries(frame_index, ...)`) and verify the slot was published
  for it, so an application calls `publish()` in its prepare phase for every
  frame it draws. `has_drawable_entries()` asserts the render phase. Shader
  stages resolve at a list's first draw (all `multiview_view_counts` at
  once) instead of at registration; the frustum cull reads the list key's
  mobility. `Draw_list_renderer::render()` and the `Draw_list` overload of
  `Primitive_buffer::update()` verify that the settings need no per-mesh
  evaluation (no `id_offset` color source, constant size); that overload no
  longer takes a `Draw_list_scene`.
- `erhe::scene_renderer`: `Shadow_renderer::render()` on the draw-list path
  reads the bounds of `Draw_list_scene::get_shadow_bounds()` for the
  device's frame index, so the application calls `prepare_shadow_bounds()`
  for that frame in its prepare phase; it no longer reads `mesh_spans` on
  that path.
- `erhe::graphics`: up to `Device::get_number_of_frames_in_flight()` device
  frames may be open at once. `Device::acquire_frame_slot(frame_id)` no
  longer requires the previous frame to have ended; it verifies frames are
  acquired in id order with at most N open, and each frame-in-flight slot
  has its own state. `Device::get_frame_index()` returns the frame of the
  calling role: the oldest open frame on a dedicated render thread, the
  newest acquired frame (or the next one while none is open) on every other
  thread. Vulkan `Device::get_command_buffer()` is fatal when the calling
  role's frame is not open.
- `erhe::scene`: `Node_transforms::get_next_serial()` / `get_current_serial()`
  are thread-safe (a mutex-protected counter); the geometry workers that
  build brush previews write node transforms beside the main thread.
- `erhe::imgui`: `Imgui_renderer::begin_frame` and `Imgui_windows::begin_frame`
  take the device frame index (`uint64_t`) instead of reading
  `Device::get_frame_index()`; `Window_imgui_host::begin_imgui_frame` no
  longer sizes the host's swapchain render pass (`update_render_pass`,
  above); `Imgui_windows::begin_frame` accepts the main thread in
  `m2_prepare` or `m3_imgui`.
- `erhe::imgui`: `Imgui_windows::draw_imgui_windows()` no longer runs the
  operations handed to `Imgui_windows::queue()`; the application calls
  `Imgui_windows::flush_queue()` where the deferred work is safe (the editor
  runs it at the start of its prepare phase).
- `erhe::scene_renderer`: `Light_set` holds `erhe::scene::Light_snapshot`
  copies: `get_lights()` returns `const std::vector<Light_snapshot>&` (was the
  `shared_ptr<Light>` vector), and `resolve()` refreshes every slot's copy on
  each call (the slot layout still recomputes only when invalidated or the
  limits changed), so the owner calls it once per frame on the thread that
  owns the lights. `Light_projections::apply`, `Light_buffer::update` and
  `Shadow_renderer::render` read the copies; `Shadow_renderer` indexes the
  projections by slot.
- `erhe::scene`: `Light::projection()` and `Light::projection_transforms()`
  are cold wrappers that capture a `Light_snapshot` and compute on it; the
  returned `Light_projection_transforms::light` is null.
- `erhe::scene_renderer`: `Joint_buffer::update` takes
  `std::span<const Joint_pose>` (was the skins span) and reads no joint node;
  `Base_render_parameters::joint_poses` and
  `Shadow_renderer::Render_parameters::joint_poses` replace the `skins`
  fields; `Forward_renderer::draw_primitives` takes
  `const erhe::scene::Light_snapshot*`.
- `erhe::scene`: `Light_projection_parameters::view_camera` is a
  `const Camera_snapshot*` (was `const Camera*`); the directional fits read
  the copy.
- `erhe::scene_renderer`: `Camera_view_input` holds `const Camera_snapshot*
  camera` (was `projection` + `node` pointers); `Camera_buffer::update` takes
  a `Camera_snapshot` (the `Projection` + `Node` overload is gone; the
  `Trs_transform` overload stays); `Light_projections::apply`,
  `Shadow_renderer::Render_parameters::view_camera`,
  `Texel_renderer::Render_parameters::camera`,
  `Cube_renderer::Render_parameters::camera` and `build_per_view_camera` take
  the snapshot.
- `erhe::renderer`: frame slots. `Debug_renderer::begin_frame()` returns the
  slot id it opened (`[[nodiscard]]`), `render()` takes `multiview` and the
  slot (no defaults), `end_frame()` is replaced by `release_frames()` (once
  per device frame, after the last `render()`, releases every slot).
  `Text_renderer::set_frame_slot()` tags prints, `render()` takes `multiview`
  and the slot and releases that slot's prints, `release_frames()` drops the
  rest. `Debug_renderer_bucket::start_view()` and `render()` take the slot.
  Several views may be recorded before any is drawn, on a thread other than
  the one that draws them.
- `erhe::frame_pacing`: `Frame_time_recorder` is thread-safe (one mutex).
  `find()` is removed: writers use `set(frame_id, &Frame_time_record::field,
  value)` (and a two-field overload), `set_once()` and `add()`, readers copy
  a record with `get(frame_id, out_record)` and test with `contains()`.
  `begin_frame(frame_id, cpu_slot_begin)` returns nothing. The display
  refresh duration is kept in the recorder
  (`set_display_refresh_duration()` / `get_display_refresh_duration()`),
  which `Device::set_display_refresh_duration_seconds()` /
  `get_display_refresh_duration_seconds()` forward to.
- `erhe::xr`: `Xr_session::begin_frame()` takes the `Frame_timing` of the
  frame it begins (`begin_frame(const Frame_timing&)`) and keeps its
  predicted display time for `render_frame()`, `render_frame_multiview()`
  and `end_frame()`, so a `wait_frame()` of the next frame on another thread
  does not change the display time of the frame being rendered.
  `Headset::begin_frame(timing)` passes its timing through.
- `erhe::utility`: `Thread_roles` keeps one frame phase per role.
  `set_frame_phase()` / `get_frame_phase()` / `assert_frame_phase()` act on
  the calling role's phase (the render phase on a dedicated render thread,
  the main phase otherwise; in inline mode the main thread writes both);
  `assert_main_thread_in_phase()` and `assert_command_recording_allowed()`
  check the main phase.
- `erhe::xr`: `Headset::begin_frame_()` is replaced by `Headset::wait_frame()
  -> Frame_timing` (`xrWaitFrame`, sets the new `Frame_timing::wait_ok`) and
  `Headset::begin_frame(const Frame_timing&) -> bool` (`xrBeginFrame` and the
  performance metrics refresh); the caller stores the result in
  `Frame_timing::begin_ok`. `Frame_timing` moved to its own header
  `erhe_xr/frame_timing.hpp` (included by `headset.hpp`), available in every
  `ERHE_XR_LIBRARY` configuration. `Xr_session::begin_frame` asserts the
  render role and `r_render` instead of the main thread in `m2_prepare`,
  and treats `XR_FRAME_DISCARDED` as a begun frame.

- `erhe::graphics`: `Command_buffer` synchronization methods renamed to
  state the edge direction and drop the self-argument: `signal_gpu(c)` is
  `signal_gpu_completion_to(c)`, `wait_for_gpu(self)` is
  `wait_for_gpu_completion_signal()`, `signal_cpu(self)` is
  `signal_cpu_completion()`, `wait_for_cpu(p)` is
  `wait_for_cpu_completion_of(p)`. The implicit primitives and the
  consumer-owned semaphore model are unchanged.
- `erhe::item`: the editor-only `Item_flags` bits (`tool`, `brush`,
  `controller`, `rendertarget`, `expand`, the hover bits,
  `show_in_developer_ui`, `show_debug_visualizations`, `affects_shadow`,
  `bone_proxy`, `active_item`, `view_anchored`, the viewport locks,
  `invisible_parent`, `render_wireframe`, `render_bounding_volume`) and the
  editor-only `Item_type` indices (`brush`, `composer`, `grid`,
  `composition_pass`, `rendertarget`, the asset and content-library
  entries, `joint`, `raytrace`, `render_style`, the graph types, `style`)
  are the editor's (`src/editor/editor_item_bits.hpp`), as are their
  flag-bridged properties; the library bits were renumbered (glTF
  serializes flags by name). `Item_flags::purpose_guide_when_set` /
  `purpose_inputs` / `transient` as constants are replaced by the
  registered masks; `derive_purpose_from_flags()` is no longer
  `constexpr`. `Item_type::count` is gone (`index_count`, `library_count`).
  `Item_type::bone`, `light_layer`, `mesh_layer`, `animation_channel` and
  `animation_sampler` (no class carried them) are removed.
- `erhe::primitive`: `Build_info::constant_color` is a `glm::vec4`;
  `build_info.hpp` no longer includes geogram.
- `erhe::scene_renderer`: `Draw_color_parameters::view_frustum_planes` is a
  `std::span<const glm::vec4>` (empty: no culling) instead of a pointer to a
  six-plane array.

### Removed

- `erhe::scene_renderer`: `Draw_list_scene::record_cull_classification()`,
  `Gpu_cull_classifier::record_pending()` and `add_pending_scene()` (the
  frame's classification is `classify_frame_cull_views()` /
  `Cull_frame_registry::classify_frame()`, before the first draw).

- `erhe::graphics`: `Device::get_active_render_pass()` /
  `set_active_render_pass()`; the active render pass is per command buffer
  (`Command_buffer::get_active_render_pass()`).

- `erhe::rendergraph`: `Rendergraph::unregister_node()` and the public
  `Rendergraph::sort()` (releasing the node's last `shared_ptr` unregisters
  it; `prepare_frame()` sorts).

- `erhe::scene_renderer`: `Content_wide_line_renderer::set_use_tent()`,
  `get_use_tent()`, `set_line_bias_margin()`, `get_line_bias_margin()`,
  `set_line_bias_clamp()`, `get_line_bias_clamp()` (the settings are a
  `set_view_params()` argument).

- `erhe::imgui`: `Imgui_renderer::at_end_of_frame()` (unused).

- `erhe::scene_renderer`: `Primitive_buffer::reset_id_ranges()`,
  `id_offset()`, `id_ranges()` and the `use_id_ranges` argument of
  `Primitive_buffer::update(const Render_bucket&, ...)`: the buffer keeps no
  id state; an id pass prepares its id ranges with `append_records(...,
  io_id_offset, id_ranges, out)`.

- `erhe::scene_renderer`: `Draw_list_scene::get_object_mesh()`,
  `Draw_list::color_resolutions` / `shadow_resolutions` /
  `color_resolution_failed` / `shadow_resolution_failed` (now
  `Draw_list_resolutions`), and the draw-time joint slot sync (done by
  `publish()`).
- `erhe::scene_renderer`: `Draw_list_scene::gather_shadow_bounds()` and
  `Shadow_bounds_gather_parameters` (the render-phase gather; replaced by
  `prepare_shadow_bounds()` / `get_shadow_bounds()`).
- `erhe::scene_renderer`: `Light_set::find_slot(const Light*)` and
  `Light_projections::get_light_projection_transforms_for_light(const Light*)`
  (the lookups by live light pointer; use the `_id` forms).
- `erhe::imgui`: `Frame_log_window::on_frame_begin()` (it was empty).
- `erhe::item`: `Item_host::hosted_selection` (the editor's `Selection`
  keeps its per-host buckets), `Item_base::is_hovered()`,
  `is_lock_viewport_selection()`, `is_lock_viewport_transform()` and the
  `lock_viewport_transform_property`, `lock_viewport_selection_property`,
  `show_debug_visualizations_property`, `show_in_developer_ui_property`
  statics (the editor's `Editor_item_properties`).

- `erhe::graphics`: `erhe_graphics/vulkan/vulkan_pipeline_cache_file.hpp`:
  `Pipeline_cache_identity`, `make_pipeline_cache_path()`,
  `read_pipeline_cache_file()` and `write_pipeline_cache_file()`, the
  per-device-identity file behind the Vulkan `Device`'s persisted
  `VkPipelineCache` (loaded at device creation, written at destruction).
- `erhe::imgui`: `Imgui_item_recorder::request_scroll_to_item()` /
  `take_scroll_to_item_result()`: in the next recorded frame, Dear ImGui's
  `ScrollToItem()` runs right after the item with the given id is submitted.
  A recorded frame now submits clipped items in full
  (`ImGuiContext::ItemUnclipByLog`), so they are recorded with their labels.
- `erhe::graphics`: `Ring_buffer_pool` (`erhe_graphics/ring_buffer_pool.hpp`),
  the ring-buffer allocator behind `Device::allocate_ring_buffer_entry()` for
  the Vulkan, OpenGL and Metal backends: spill sizing and idle reclaim (one
  warm buffer per usage class), which only the Vulkan backend had.
- `erhe::geometry`: `erhe_geometry/operation/flip_facets.hpp`:
  `flip_facets()` and `recalculate_facet_normals()` with `Normal_side`
  (outside / inside): flip and recalculate normals of
  `doc/plans/mesh_modeling.md` catalog M10, composed on `Edit_mesh`, keeping
  every index and carrying the selection through `Component_remap`;
  `Edit_mesh::reverse_facet()`.
- `erhe::geometry`: `erhe_geometry/operation/smooth_vertices.hpp`:
  `smooth_vertices()` with `Smooth_vertices_options` (factor, repeat): the
  new positions of the selected vertices moved toward their neighbours'
  average (catalog M10), without changing the geometry.
- `erhe::geometry`: `erhe_geometry/operation/bridge_loops.hpp`:
  `bridge_loops()` with `Bridge_loops_options` (`Bridge_connection`
  open loop / closed loop / loop pairs, merge, merge factor, twist offset,
  cuts) and `Bridge_loops_result`: bridge edge loops of
  `doc/plans/mesh_modeling.md` section 4.10 (facet selections deleted and
  their region boundaries bridged, unequal loops with beautified triangles,
  merge through the weld core, cuts through `subdivide_edges()`), composed on
  `Edit_mesh`, selecting the bridge facets through `Component_remap`.
- `erhe::geometry`: `erhe_geometry/operation/fill.hpp`: `fill_selection()`
  with `Fill_result`: fill (F) of `doc/plans/mesh_modeling.md` section 4.10
  (two vertices closing a boundary chain, a free vertex plus a chain, edge
  cycles / chains / nets, selected facets joined, vertices sorted radially),
  composed on `Edit_mesh`, selecting the new facets through `Component_remap`.
- `erhe::geometry`: `erhe_geometry/operation/connect_vertices.hpp`:
  `connect_vertices()`, `connect_vertex_pair()` and `connect_selection()`:
  connect vertex path (J) of `doc/plans/mesh_modeling.md` section 4.10
  (facet splits between selected corners; a cutting-plane best-first path
  between two vertices sharing no facet), selecting the new edges through
  `Component_remap`.
- `erhe::geometry`: `erhe_geometry/operation/bevel_edges.hpp`: `bevel_edges()`
  with `Bevel_edges_options` (`Bevel_offset_type` offset / width, amount,
  loop slide) and `Bevel_edges_result`: the first version of the edge bevel of
  `doc/plans/mesh_modeling.md` section 4.9 (M13a: edges only, one segment),
  composed on `Edit_mesh`, reporting the new vertices with their directions
  per unit amount, the edge facets and the vertex facets, and selecting the
  edge facets through `Component_remap`.
- `erhe::geometry`: `Knife_cut::end_polyline()`, `Knife_cut::close_polyline()`
  and `Knife_cut::get_polyline_count()`: several polylines per knife cut, each
  ended or closed on its own; `undo_last_point()` reopens the polyline of the
  removed point, and `Knife_options::close_polyline` closes the current
  polyline.
- `erhe::geometry`: `erhe_geometry/operation/knife_cut.hpp`: `Knife_cut` (an
  `Edit_mesh_operation` driven point by point: `add_point()`,
  `undo_last_point()`, `get_preview_segments()`, `finish()`) and the one-shot
  `knife_cut()`, with `Knife_view`, `Knife_point` / `Knife_snap`,
  `Knife_options` and `Knife_result`: the geometric core of the knife of
  `doc/plans/mesh_modeling.md` section 4.7 (segment-to-cuts through the
  view's cut plane with pixel tolerances, occlusion unless cut through,
  per-facet pairing with the concave midpoint rule, apply through
  `split_facet_edgenet()`), carrying a component selection through
  `Component_remap`.
- `erhe::geometry`: `erhe_geometry/operation/split_components.hpp`:
  `split_facets()`, `split_edges()`, `rip_vertices()` with `Rip_options`,
  `extract_facets()` and `get_selection_facets()`: split, rip and separate of
  `doc/plans/mesh_modeling.md` catalog M9, composed on `Edit_mesh`, carrying a
  component selection through `Component_remap` (split and rip select the
  duplicated side).
- `erhe::geometry`: `erhe_geometry/operation/inset_faces.hpp`: `inset_faces()`
  with `Inset_faces_options` and `Inset_faces_result`: inset faces of
  `doc/plans/mesh_modeling.md` section 4.8 (region and individual, boundary,
  even and relative offset, edge rail, thickness, depth, outset,
  interpolate), composed on `Edit_mesh`, reporting the inset vertices with
  their thickness and depth directions, the inset facets and the rim facets,
  and carrying a component selection through `Component_remap`.
  `Edit_mesh::create_facet_from_corners()`, `set_facet_vertices()` and
  `set_corner_sources()`, and the free functions
  `compute_mean_value_weights()` and `compute_newell_normal()`
  (`erhe_geometry/edit_mesh.hpp`).
- `erhe::geometry`: `erhe_geometry/operation/subdivide_edges.hpp`:
  `subdivide_edges()` with `Subdivide_edges_options` and
  `Subdivide_edges_result`, and `get_selection_edges()`: subdivide edges of
  `doc/plans/mesh_modeling.md` section 4.5 (cuts, smoothness, only quads, the
  per-facet fill patterns including grid fill), composed on `Edit_mesh`,
  reporting the inner vertices, edges and facets and carrying a component
  selection through `Component_remap`.
- `erhe::geometry`: `erhe_geometry/operation/merge_vertices.hpp`: `merge_vertices()`
  with `Merge_type` (`at_center`, `at_position`, `at_first`, `at_last`,
  `collapse`) and `Merge_vertices_options`, and `merge_by_distance()` with
  `Merge_by_distance_options`: the merge operations of
  `doc/plans/mesh_modeling.md` section 4.4, composed on `Edit_mesh`, carrying a
  component selection through `Component_remap`. `Edit_mesh::set_vertex_sources()`
  replaces a scratch vertex's provenance; `Edit_mesh_operation::get_emitted_vertex()`
  returns the destination vertex of a scratch vertex after `emit()`.
- `erhe::geometry`: `erhe_geometry/operation/dissolve.hpp`: `delete_components()`,
  `dissolve_faces()`, `dissolve_edges()`, `dissolve_vertices()` and
  `dissolve_limited()` with the option classes `Dissolve_faces_options`,
  `Dissolve_edges_options`, `Dissolve_vertices_options` and
  `Dissolve_limited_options`: the delete and dissolve operations of
  `doc/plans/mesh_modeling.md` section 4.3, composed on `Edit_mesh`, carrying a
  component selection through `Component_remap`.
- `erhe::geometry`: `Geometry_operation::remap_component_selection()` keeps a
  remapped edge (or sub-edge) only when the destination has that edge, when
  the destination has an edge table: a merged or dissolved edge is no longer
  reported as selected while its surviving endpoints still are.
- `erhe::geometry`: `Geometry_operation::propagate_edge_sharpness_identity()`,
  called at the end of `post_processing()`: a source edge's `edge_sharpness`
  is set on the destination edge whose two vertices each derive with weight 1
  from that edge's two vertices, so creases survive every operation that keeps
  the edge (triangulate, kis), not only Catmull-Clark.
- `erhe::graphics`: `Alpha_mode` (`premultiplied` | `straight`) and a trailing
  `alpha_mode` parameter on both `Image_loader::open()` overloads (default
  `premultiplied`, the previous behavior): `straight` makes PNG / JPEG decode
  return the stored RGBA bytes without multiplying RGB by alpha.
- `erhe::graphics`: `Device_info::use_depth_clamp`: whether
  `Rasterization_state::depth_clamp_enable` is honoured (Vulkan: the
  `depthClamp` device feature; always true on OpenGL and Metal).
- `erhe::renderer`: `Minor_lines` and `Primitive_renderer::set_minor_lines()`
  (default `draw`): `skip` makes `add_sphere`, `add_cone`, `add_capsule` and
  `add_torus` emit only their self-visible major-style lines.
- `erhe::renderer`: `Primitive_renderer::set_minor_line_renderer(other)`:
  the shape helpers emit their minor-style lines through `other`'s bucket,
  so a bucket one stencil reference below lets the major-style lines win
  every shared pixel regardless of emission order.
- `erhe::renderer`: `Anti_aliasing` and
  `Debug_renderer::set_anti_aliasing()` / `get_anti_aliasing()` (default
  `on`): wide debug lines are drawn with analytic, energy-conserving
  coverage over a one-pixel fringe and fade below one pixel wide; `off`
  keeps the rasterized binary edge. The hidden pass dims in a fragment
  shader variant instead of a constant blend factor. Each pass is a core
  draw (fully covered fragments, stencil `greater_or_equal`: last fragment
  wins, so translucent lines of one bucket blend where they overlap) and a
  fringe draw (partial fragments, stencil `greater`: first fringe wins and
  never over a core), so overlapping fringes at polyline joints do not blend
  twice (`doc/erhe/renderer.md` "Line anti-aliasing").
- `erhe::graphics`: `Device_info::sub_pixel_precision_bits` (Vulkan
  `subPixelPrecisionBits`, `GL_SUBPIXEL_BITS`; 0 = not reported, Metal).
  `Shadow_renderer` logs an error below the 8 bits its caster vertex snap
  bound assumes, and a warning when the value is not reported.
- `erhe::scene_renderer`: `Light_shadow_limits::caster_vertex_rounding`,
  `Caster_vertex_extent` and a trailing `Light_projections::apply()`
  parameter `in_caster_vertex_extents` (one per caster mesh: the upper 3x3
  and translation of its `world_from_node`, the largest `|coordinate|` per
  axis of its node-space bounds): the per-light bound on the fp32 rounding
  of any caster vertex position, from the vertices' distance from their node
  origin, written to the light block's new `shadow_limits.x` and read by the
  minimum bias's position term. `Shadow_renderer::render()` gathers the
  extents with the caster bounds (every caster, whatever the fit settings).
- `erhe::scene_renderer`: `Light_shadow_limits` and
  `Light_projections::light_shadow_limits` (parallel to the slots, derived
  once per `apply()`): `raster_vertex_depth`, the vertex depth bound of the
  minimum bias's raster term (the largest caster-bounds depth for a
  depth-clamped directional pass, else 1; written to the light block's
  `view_origin.w`).
- `erhe::scene_renderer`: `Light_shadow_limits::distance_rays_valid`,
  `get_spot_distance_min_resolution(outer_spot_angle, Shadow_map_footprint)`
  and `Light_projections::report_distance_fallbacks()` /
  `reset_distance_fallbacks()`: the distance technique's minimum spot map
  resolution per cone angle and filter; a spot light below it is sampled with
  the depth technique (light block `shadow_index_packed.z` = 0).
- `erhe::scene`: `Shadow_map_footprint` (`erhe_scene/light.hpp`, the receiver
  filter's tap reach with its border width and coverage margin) and
  `Light_projection_parameters::shadow_map_footprint`;
  `erhe::scene_renderer`: `Shadow_renderer::Render_parameters::shadow_map_footprint`
  and a trailing `Light_projections::apply()` parameter of the same name. The
  2D shadow pass border is the footprint's border width, and the directional
  fit and spot projection keep covered receivers the coverage margin inside
  the map, so the spot frustum is wider than the outer cone and the stable
  directional projection wider than `2 * shadow_range`.
- `erhe::scene_renderer`: `get_shadow_depth_bits_axis(erhe::dataformat::Format)`
  (`erhe_scene_renderer/shader_key.hpp`): the `ERHE_SHADOW_DEPTH_BITS` variant
  axis value of a shadow map depth format (16 / 24 UNORM, 32 float, 0 none).
  `Forward_renderer::Render_parameters::shadow_depth_bits`,
  `Prewarm_parameters::shadow_depth_bits` and
  `Draw_list_renderer::Render_parameters::shadow_depth_bits` take this value
  for the shadow map actually sampled, not a requested bit count.
- `erhe::scene_renderer`: `Shader_debug::world_position` (36): the fragment
  world position as the color. `Base_render_parameters::shadow_debug_light_index`,
  the `Light_buffer::update()` parameter of the same name and
  `Light_block::shadow_debug_light_index` (`light_block.shadow_debug_light_index`
  in GLSL): the light slot `Shader_debug::shadow_visibility` shows, which now
  covers any shadow-mapped light including point lights.
- `erhe::graphics`: `Gpu_timer(Device&, const char* label)`,
  `Gpu_timer::begin()` / `end()` and `Scoped_gpu_timer`
  (`erhe_graphics/gpu_timer.hpp`): a GPU timer for an explicit range of one
  command buffer, such as a range of compute dispatches, next to the
  `Render_pass`-bound form. `Gpu_timer_impl` backends take a `Device&` instead
  of a `Render_pass&`.
- `erhe::geometry`: `Geometry::has_connectivity()` and
  `Geometry::has_edge_connectivity()` (`erhe_geometry/geometry.hpp`): whether
  the corner / edge connectivity tables are built for the current element
  counts.
- `erhe::geometry`: `Mesh_attributes::for_each_facet_attribute()`,
  `for_each_vertex_attribute()`, `for_each_corner_attribute()` and
  `for_each_edge_attribute()` (`erhe_geometry/geometry.hpp`): enumerate one
  element domain's attributes as (member name, `Attribute_present<T>&`); the
  const overloads pass `const Attribute_present<T>&`.
- `erhe::physics`: `Drive_force_mode` and `Constraint_axis_drive::mode`
  (`erhe_physics/iconstraint.hpp`): the KHR_physics_rigid_bodies drive
  `force` / `acceleration` mode, honored by both backends.
- `erhe::physics`: `fold_fixed_axis_values()`, `Fixed_axis_fold` and
  `restore_folded_fixed_values()` (`erhe_physics/joint_limits.hpp`): a fixed
  axis authored at a non-zero value folds into frame A, so both backends hold
  it at that value.
- `erhe::physics`: `Translation_limit_model`, `Joint_limit_shape::translation_model`
  / `distance` and `Joint_coordinates::distance` (`erhe_physics/joint_limits.hpp`):
  the sphere the Box3D distance joint enforces.
- `erhe::physics`: `Six_dof_joint_kind::distance`, `Six_dof_classification::min_distance`
  / `max_distance`, `damping_to_ratio()`, `Box3d_spring` and
  `drive_to_box3d_spring()` (`erhe_physics/box3d_six_dof_classifier.hpp`);
  `Six_dof_classification::axis` is the twist axis of a spherical joint.
- `erhe::physics`: `velocity_drive_gain()` (`erhe_physics/joint_limits.hpp`),
  and `Box3d_constraint::prepare_step()` / `Jolt_constraint::prepare_step()`,
  called by the world's `update_fixed_step()` before the engine step: a
  velocity drive's motor force bound follows the current velocity error.
- `erhe::scene_renderer`: view-relative positions (`doc/erhe/shadows.md`
  "View-relative positions"). `get_view_origin()` and
  `get_clip_from_view_relative()` (`erhe_scene_renderer/camera_buffer.hpp`);
  `Camera_struct::view_origin` / `clip_from_view_relative` (camera block);
  `Light_struct::view_origin` / `texture_from_view_relative` /
  `view_relative_from_texture` (light block), composed once per
  `Light_projections::apply()` into `Light_view_relative_transforms`
  (`Light_projections::light_view_relative_transforms`).

- `erhe::geometry`: `erhe_geometry/topology.hpp` topology walkers
  `walk_edge_loop()`, `walk_edge_ring()`, `walk_face_loop()`,
  `walk_boundary_loop()` and `walk_connected_region()`, with the delimit
  flag types `Edge_loop_delimit` / `Region_delimit` and the `Walk_shape`
  result (`open` | `closed`); and `Geometry::get_corner_edge(corner)`, the
  edge of the facet edge starting at a corner, valid until the next
  `build_edges()`.
- `erhe::geometry`: `erhe_geometry/edit_mesh.hpp` `Edit_mesh`, a mutable
  polygon mesh scratch with provenance, loaded from a `Geometry`, with the
  primitives `split_edge()`, `split_facet()`, `split_facet_edgenet()`,
  `join_facets()`, `join_facet_pair()`, `collapse_vertex()`,
  `weld_vertices()`, `separate_vertex()`, `delete_elements()` (with
  `Delete_context`) and `create_facet()`, their result enums
  `Join_result`, `Collapse_result` and `Edgenet_result`, and adjacency
  queries; and `operation::Edit_mesh_operation`, a `Geometry_operation`
  whose `emit()` writes the scratch into the destination through the
  provenance tables.

### Changed

- `erhe::graphics`: `Texture_create_info::make_view()` takes the view's base
  level and base array layer (default 0) and sets the view's extents to the
  source's at that level and its level and layer counts to what remains past
  the bases; a view whose extents do not match its image level is rejected.
- `erhe::graphics`: `device.hpp` forward-declares `Graphics_config`,
  `Surface_create_info`, `Shader_monitor`, `Shader_source_cache`, `Spirv_cache`
  and `erhe::frame_pacing::Frame_time_recorder` instead of including their
  headers (and no longer includes `buffer.hpp`, `surface.hpp`,
  `swapchain.hpp` or `erhe_math/math_util.hpp`); a translation unit using
  one of them includes its header. `Present_wait_result` moved from
  `swapchain.hpp` to `enums.hpp`. `Texture_reference` and
  `Texture_reference_user` moved to `erhe_graphics/texture_reference.hpp`,
  which `texture.hpp` includes.
- `erhe::math`: `Coordinate_conventions`, `Depth_range`, `Framebuffer_origin`,
  `Texture_origin` and `Clip_space_y_flip` moved from `math_util.hpp` to
  `erhe_math/coordinate_conventions.hpp`, which `math_util.hpp` includes.
- `erhe::graphics`: `Spirv_cache::get()` and `Spirv_cache::put()` take a
  `uint64_t compile_settings_hash` that is part of the entry key (the hash
  of every glslang setting affecting the output, computed by
  `glsl_to_spirv.cpp`); `put()` writes entries atomically.
- `erhe::graphics`: the three nine-parameter `Blit_command_encoder` copies
  take region value types: `copy_from_texture(Texture_location source,
  glm::ivec3 size, Texture_location destination)`,
  `copy_from_buffer(Buffer_texel_location, size, Texture_location)` and
  `copy_from_texture(Texture_location, size, Buffer_texel_location)`.
  `Texture_location` names a texture, slice, level and origin;
  `Buffer_texel_location` a buffer, offset, bytes per row and bytes per image.
- `erhe::math`: `Aabb::is_valid()` is replaced by `Aabb::is_valid_3d()`, which
  requires `min <= max` on all three axes (the old check accepted a box valid
  on any one axis, and a box with a NaN bound).
- `erhe::scene_renderer`: `Mesh_memory` rejects a temporary config at compile
  time (deleted rvalue constructor); it keeps a reference to the config.
- `erhe::scene_renderer`: draw-list color passes cull entries against the
  view frustum: `Draw_color_parameters::view_frustum_planes` (set by
  `Draw_list_renderer` for single-view passes), `Draw_statistics::culled_count`,
  and `Draw_list_entry::world_aabb` kept current by the transform hook. The
  draw-list overloads of `Primitive_buffer::update()` and
  `Draw_indirect_buffer::update()` take a per-entry pass mask
  (`std::span<const std::uint8_t>`) instead of the `Item_filter`.
- `erhe::graphics`: `Scoped_debug_group` / `Scoped_queue_debug_group` take
  the label as `std::string_view` (or `const Debug_label&`) and keep no copy;
  the backends copy it onto the stack (`erhe_graphics/debug_label_buffer.hpp`).
- `erhe::scene_renderer`: `bucket_primitives()` fills a `Render_bucket_list`
  (reused bucket storage, `clear()` keeps capacity) instead of a
  `std::vector<Render_bucket>`; `Render_bucket::reset()` added.
- `erhe::scene`: `Light`'s getters read a mirror of the effective property
  values, refreshed by the shared changed callback.
- `erhe::scene`: the copy constructors and copy assignments of `Xformable`,
  `Xform`, `Boundable`, `Gprim`, `Point_instancer`, `Light`, `Camera` and
  `Scene` are deleted (they aborted at run time); cloning goes through the
  `for_clone` constructors; `Scene` is `Item_kind::not_clonable` (`clone()`
  returns null). `Node_data::diff_mask()` and
  `Node_data::bit_transform` removed.
- `erhe::graphics`: `Scoped_buffer_mapping` (`erhe_graphics/scoped_buffer_mapping.hpp`)
  removed; it had no users.
- `erhe::physics`: `IWorld::debug_draw()` takes `(IDebug_draw&, glm::vec3
  camera_position)` instead of `erhe::renderer::Jolt_debug_renderer&`;
  `IDebug_draw` is reduced to `draw_line(from, to, color)` (RGBA), which the
  application implements over its line renderer. The Jolt backend adapts
  `JPH::DebugRenderer` itself (`erhe_physics/jolt/jolt_debug_renderer.hpp`,
  `get_jolt_debug_renderer()`), so `erhe_physics` no longer links
  `erhe::renderer`.
- `erhe::renderer`: `Jolt_debug_renderer` (`erhe_renderer/jolt_debug_renderer.hpp`)
  removed; `erhe_renderer` no longer links Jolt.
- `erhe::window`: `copy_to_clipboard()` moved from `erhe::utility`
  (`erhe_utility/clipboard.hpp`) to `erhe_window/clipboard.hpp`, namespace
  `erhe::window`; `erhe_utility` no longer links SDL.
- `erhe::geometry`: the Geogram `fmt::formatter` specializations moved from
  `erhe_log/log_geogram.hpp` to `erhe_geometry/geogram_format.hpp`;
  `erhe_log` no longer links geogram.
- `erhe::commands`: the XR bindings (`Xr_boolean_binding`, `Xr_float_binding`,
  `Xr_vector2f_binding`, `bind_command_to_xr_*_action()`,
  `get_xr_*_bindings()`) exist only with `ERHE_XR_LIBRARY_OPENXR`; without
  OpenXR `erhe_commands` does not link `erhe::xr`.
- `erhe::verify`: `ERHE_FATAL` / `ERHE_VERIFY` report through
  `erhe_report_fatal()`, which also passes the message and callstack to a
  handler set with `erhe_set_fatal_handler()`; `erhe::log::initialize_log_sinks()`
  installs one that writes them to `logs/log.txt`.
- `erhe::graph` links `erhe::item` PUBLIC; `erhe::item` no longer links
  `erhe::message_bus`, `erhe::rendergraph` no longer links `erhe::ui`, and
  `erhe::ui` no longer links `erhe::primitive`.
- `erhe::geometry`: `bevel_edges()` (`erhe_geometry/operation/bevel_edges.hpp`)
  gains segments and profile (`doc/plans/mesh_modeling.md` section 4.9, M13b):
  `Bevel_edges_options::segments` (default 1, the unchanged one segment
  bevel) and `Bevel_edges_options::profile` (default 0.5; a superellipse with
  exponent `1 / (1 - profile)`, 0 chamfer, 0.5 quarter circle, 1 square
  corner); each beveled edge becomes a strip of `segments` quads, a vertex
  with three or more beveled edges on a closed fan is filled by the cutoff
  patch, and an unbeveled edge shortened by the bevel keeps its sharpness.
  `Bevel_edges_result::boundary_vertices` / `boundary_directions` include the
  profile samples (directions exact for any amount), `edge_facets` lists every
  strip quad, `vertex_facets` includes the patch facets, and
  `Bevel_edges_result::beveled_edges` counts the beveled edges.
- `erhe::primitive`: a `GEO::Mesh` / `Geometry` with no facets builds to a
  valid empty primitive instead of failing or aborting: `build_buffer_mesh()`
  and `Primitive::make_renderable_mesh()` succeed with an empty `Buffer_mesh`
  (no ranges, no allocations), and `Primitive_raytrace(const GEO::Mesh&)` /
  `Primitive::make_raytrace()` succeed with no `IGeometry` (previously a
  zero-size `Cpu_buffer` aborted). `Build_context::is_empty()` added.
- `erhe::gltf`: `export_gltf()` skips a mesh primitive whose geometry has no
  facets, and writes a node whose mesh has no exportable primitive without a
  `mesh` (previously an empty glTF mesh, which fails to re-import).
- `erhe::commands`: key bindings with a modifier mask dispatch before key
  bindings without one (declaration order within each group), so a chord
  such as Ctrl+A reaches its binding even when a mask-less A binding, which
  matches any modifiers, was declared first.
- `erhe::renderer`: `Primitive_renderer::add_cone` and `add_capsule` classify
  their structural surface lines exactly instead of drawing them all in
  minor style: cone cap cross lines follow their cap's visibility and
  lateral generatrices the lateral facing test at their azimuth; capsule
  junction rings are split at the silhouette azimuths, cap profile arcs at
  the cap sphere's horizon, and generatrices follow the tangent-plane test.
  Only the axis stays minor.
- `erhe::renderer`: `Debug_renderer_config::primitive_type` defaults to
  `Primitive_type::line` (was `Primitive_type{0}`, i.e. `point`) and must be
  `line` or `triangle`; any other type fails an `ERHE_VERIFY` when its bucket
  is created. The point path, which never set a point size, is removed.
- `erhe::scene_renderer`: `Shadow_frustum_fit_settings::depth_clamp` selects
  the depth-clamp pipelines for the directional light passes only; spot
  passes always clip. `Shadow_renderer::render()` gathers the caster bounds
  when `depth_clamp` is on as well as with `fit_to_casters`.
- `erhe::scene_renderer`: `standard.vert` computes positions relative to the
  pass's view origin (relative to eye: the fp32 node translation minus the
  fp32 view origin, `precise`) and `gl_Position` from
  `clip_from_view_relative`, and the shadow
  casters and receivers (`sample_light_visibility()`,
  `sample_point_light_visibility()`, which now take the view-relative receiver
  position) work relative to the camera and to the light camera, so the
  shadow bias bounds no longer grow with the distance from the world origin.
  `Light_buffer::update_control()` drops `point_light_position`: the cube
  caster reads its face resolution from `shadow_map_resolution`.
- `erhe::scene`: `Light::get_texture_from_clip()` and
  `Light::get_clip_from_texture()` are public.

- `erhe::scene_renderer`: the `distance` shadow technique covers spot lights
  and stores, per texel of the R32F distance map, the caster plane's light
  distance on the texel's centre ray (radial for spot, linear light-space depth
  in world units for directional; `res/shaders/erhe_shadow_distance.glsl`),
  compared by `sample_light_visibility()` against the receiver plane on the
  same rays with derived bounds (`doc/erhe/shadows.md` "The distance
  technique"). `Shadow_renderer::Render_parameters::distance_bias_coeff` is
  removed; the second parameter of `Light_buffer::update_control()` and the
  `light_control_block` field are now `shadow_map_resolution` (was
  `shadow_distance_bias_coeff`), and the distance map and its fallback are
  cleared to `1e30`. The `VARIANT_SHADOW_CUBE` caster stores the farther of
  its plane's radial distance on the centre ray and its interpolated point's
  own distance (`doc/erhe/point_light_shadows.md` "Stored distance").
- `erhe::scene_renderer`: point-light cube shadows. The `VARIANT_SHADOW_CUBE`
  caster stores its primitive plane's radial distance on each texel's centre
  ray, and `sample_point_light_visibility()` (`res/shaders/erhe_light.glsl`)
  takes the receiver plane `vec4` of `get_receiver_geometric_normal()` as a
  fourth argument and replaces the fixed `max(0.05, 0.02 * distance)` bias
  with the derived one (`doc/erhe/point_light_shadows.md` "Receiver bias").
  The w component of `Light_buffer::update_control()`'s
  `point_light_position` is the cube face resolution in texels (was the far
  distance). New `c_min_point_shadow_resolution` (64,
  `erhe_scene_renderer/light_buffer.hpp`): the smallest face resolution the
  derivation holds for. New shader include `res/shaders/erhe_point_shadow.glsl`.
- `erhe::scene_renderer`: `Shadow_renderer::Render_parameters::cull_mode`
  defaults to `Shadow_cull_mode::cull_back` (was `cull_front`, which leaks
  light where a caster touches a receiver; `doc/erhe/shadows.md` "Shadow pass
  mechanics").
- `erhe::scene_renderer`: the receiver's minimum shadow bias
  (`doc/erhe/shadows.md` "Minimum bias"). `Shadow_renderer::Render_parameters`
  gains `shadow_bias_texel_scale` / `shadow_bias_origin_scale` (default 1),
  which `Shadow_renderer::render()` stores into the same-named
  `Light_projections` fields; `Light_buffer::update()` writes them to
  `Light_block::shadow_bias_scales` (`light_block.shadow_bias_scales` in
  GLSL). In `res/shaders/erhe_light.glsl`, `get_receiver_geometric_normal()`
  returns `vec4` (unit normal, error bound in radians; 0 when the plane is
  undetermined) and `sample_light_visibility()` takes that `vec4` in place of
  the `vec3` normal.
- `erhe::scene_renderer`: `Shadow_renderer::Render_parameters::depth_bias_constant`
  / `depth_bias_slope` are signed toward the light under either depth
  convention (negative moves the stored caster depth away from the light);
  `Shadow_renderer::render()` negates them for forward-Z. They were passed to
  the device unconverted, which flipped their meaning under forward-Z.
- `erhe::scene_renderer`: `Ddgi_parameters::tiles_per_row`
  (`erhe_scene_renderer/light_buffer.hpp`), written to
  `light_block.ddgi_texels.z`: the probe field atlases place probe
  (x, y, z) at tile index `x + counts.x * (z + counts.z * y)` wrapped into
  rows of `tiles_per_row` tiles (`res/shaders/erhe_ddgi_tiles.glsl`), so a
  large grid stays within the texture size limit. `Ddgi_parameters::is_valid()`
  requires `tiles_per_row > 0`.
- `erhe::physics`: `Physics_joint_settings` no longer warns about acceleration
  mode drives; the mirror carries the mode.

- `erhe::commands`: `Binding_desc` (`erhe_commands/binding_desc.hpp`), a value
  description of one binding with a compact text form (`to_string()`,
  `parse()`) and a display label (`to_display_string()`), plus
  `Binding_kind`, `Input_kind` and `get_input_kind()`.
- `erhe::commands`: user-editable bindings on `Commands`:
  `get_input_kind()`, `has_binding_override()`, `get_default_bindings()`,
  `get_effective_bindings()`, `set_binding_override()`,
  `clear_binding_override()`, `clear_all_binding_overrides()`,
  `get_binding_overrides()`, `apply_binding_overrides()` and
  `get_binding_conflicts()`, with the `Binding_override` and
  `Binding_conflict` types; `Binding_desc::overlaps()`;
  `Menu_binding::get_shortcut_label()` / `set_shortcut_label()`;
  `Commands::add_bindings_changed_callback()`, called after every dispatch
  table rebuild.
- `erhe::imgui`: `to_erhe_keycode(ImGuiKey)` and `to_erhe_mouse_button(int)`
  (`erhe_imgui/imgui_host.hpp`), the inverse of the input mapping
  `Imgui_host` feeds ImGui with.
- `erhe::window`: `keycode_from_string()` and `mouse_button_from_string()`,
  the inverses of `c_str(Keycode)` / `c_str(Mouse_button)`.

- `erhe::codegen`: `Config_persistence` (`read_write`, `read_only`) with
  `set_config_persistence()` / `get_config_persistence()`
  (`erhe_codegen/config_persistence.hpp`), a process-wide policy for
  configuration files.
- `erhe::imgui`: `Imgui_host::load_pending_imgui_ini()` (protected); a derived
  host calls it right before `ImGui::NewFrame()`.
- `erhe::imgui`: `is_input_owned_elsewhere(ImGuiWindow*)`
  (`erhe_imgui/imgui_helpers.hpp`) and
  `Imgui_window::is_input_owned_elsewhere()`: whether Dear ImGui is in the
  middle of an interaction (an active item, a held mouse button) that the
  window does not own.
- `erhe::physics`: `get_contract_joint_limits()` - the joint contract (the
  six per-axis limits in the D6 convention: twist about X, independent swing
  about Y and Z) as a `Joint_limit_shape`, independent of the backend; and
  `describe_box3d_incompatibility()` (`erhe_physics/box3d_six_dof_classifier.hpp`),
  why Box3D would not simulate given limits exactly.
- `erhe::physics`: `get_enforced_joint_limits()`, `Joint_limit_shape`,
  `Swing_limit_model`, `Joint_coordinates`, `measure_joint_coordinates()`,
  `Joint_range_check`, `check_joint_range()`, `is_within()`,
  `get_swing_axes()`, `pyramid_swing_rotation()` and `pyramid_swing_direction()`
  (`erhe_physics/joint_limits.hpp`): the six-DOF limits the built backend
  enforces, and the current joint coordinates measured in its convention.
- `erhe::renderer`: `View::pixel_scale`, physical pixels per logical pixel of
  the view's render target (default 1.0).
- `erhe::scene_renderer`: `Camera_view_input::pixel_scale` (default 1.0);
  `Content_wide_line_renderer` multiplies a negative (screen-space) line width
  by it.

### Changed

- `erhe::physics`: the Box3D six-DOF classifier header moved from
  `erhe_physics/box3d/box3d_six_dof_classifier.hpp` to
  `erhe_physics/box3d_six_dof_classifier.hpp` and is compiled into every build,
  not only the Box3D backend.

- `erhe::renderer`: `Debug_renderer::view_from_camera()` takes a
  `pixel_scale` parameter after `viewport` (stored in `View::pixel_scale`).
- `erhe::codegen`: `save_config()` writes nothing and returns false while the
  config persistence policy is `read_only`.
- `erhe::imgui`: under a `read_only` config persistence policy an
  `Imgui_host` reads its layout ini before its first frame and never writes
  it (`io.IniFilename` stays null).
- `erhe::commands`: `Commands::bind_command_to_key()` and
  `bind_command_to_mouse_button()` take a `Button_trigger` in place of the
  `bool pressed` / `bool trigger_on_pressed` argument; a key binding can
  trigger on `Button_trigger::Any` and the command reads the pressed state
  from `Input_arguments::variant::button_pressed` (key events now fill it and
  the modifier mask). `Key_binding::get_pressed()` is replaced by
  `get_trigger()`; `Key_binding::get_modifier_mask()` and
  `Mouse_button_binding::get_trigger()` are added. `test_button_trigger()` is
  added next to `Button_trigger`.
- `erhe::commands`: `Commands::register_command()` requires a unique,
  non-empty command name and aborts on a duplicate.
- `erhe::commands`: the key, mouse and controller `bind_command_to_*()` calls
  declare default bindings; the dispatch tables (`get_key_bindings()`,
  `get_mouse_bindings()`, ...) are rebuilt from defaults and overrides at the
  start of the next `tick()` or by `sort_bindings()`, so they are empty until
  then. Binding one command to inputs of two kinds (e.g. a key and a mouse
  drag) aborts.

### Fixed

- `erhe::graphics`: on OpenGL a `Texture_create_info::sample_count` of 1
  creates a single-sample texture, as on Vulkan and Metal (it selected the
  multisample target, so a 1-sample depth attachment paired with a 0-sample
  color attachment made an incomplete framebuffer); the OpenGL
  `Render_pass::get_sample_count()` treats attachment counts 0 and 1 as the
  same single-sample count and returns 1 for a pass without attachments.
- `erhe::primitive`: building a Geometry from a `Triangle_soup` whose
  indices do not start at 0 (or skip vertices) no longer indexes outside its
  tables: positions are colocated over the used vertices only, and only the
  used vertices' attributes are read.
- `erhe::gltf`: `parse_gltf` skips a primitive with an index at or past its
  vertex count (logged) instead of reading outside the vertex data.
- `erhe::geometry`: `Geometry::build_edges()` creates one edge per vertex
  pair when two facets traverse the pair in the same direction (a winding
  flip between them); it created a second, facet-less edge for the pair.

- `erhe::geometry`: `Geometry::update_connectivity()` orders the corners
  of every vertex into a fan (`get_vertex_corners()`); it stopped at the
  first vertex with fewer than three corners, leaving the corners of every
  later vertex of an open mesh unordered. An open fan (boundary vertex)
  runs from one boundary edge to the other.

- `erhe::commands`: `Command_binding::c_type_strings` misses no `Type` any
  more (it lacked `Controller_axis` / `Controller_button`), and
  `Controller_button_binding::get_type()` returns `Type::Controller_button`
  instead of `Type::Xr_boolean`.

- `erhe::renderer`: a negative `Primitive_renderer::set_thickness()` (constant
  screen-space width) scaled with the viewport width and the camera field of
  view; it is now `-thickness` logical pixels times `View::pixel_scale`.
