# Changelog

All notable changes to the public API of the erhe libraries (`erhe::*`,
`src/erhe/`) are recorded here. The editor and the other executables are not
covered. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the rule for adding entries is in `doc/README.md` ("Changelog").

## [Unreleased]

### Removed

- `erhe::scene_renderer`: `get_position_quantization()` (read
  `erhe::primitive::Buffer_mesh::position_quantization`, or call
  `erhe::primitive::get_position_quantization()`);
  `erhe::scene_renderer::Position_quantization` is now an alias of
  `erhe::primitive::Position_quantization`.
- `erhe::scene_renderer`: `Draw_list_scene::Shadow_record_scope` and
  `Draw_list_scene::begin_shadow_record_scope()`. The shared draw records
  are frame-wide: the first shared record a `Draw_list_scene` acquires in a
  frame registers it with its `Cull_frame_registry`, and
  `Cull_frame_registry::release_frame_records()` releases them.
- `erhe::graphics`: `Render_pipeline_create_info::color_usage_before`,
  `color_usage_after`, `depth_usage_before` and `depth_usage_after` (no
  backend read them; render pass barriers come from the
  `Render_pass_descriptor` attachments).
- `erhe::scene_renderer`: `Forward_renderer::Warmup_target::color_usage_before`,
  `color_usage_after`, `depth_usage_before` and `depth_usage_after`.
- `erhe::graphics`: the Metal GLSL preamble no longer defines `ERHE_DRAW_ID`
  and no longer maps a `push_constant` block to buffer index 15; a shader
  with a `push_constant` block fails to compile on Metal. Both Metal devices
  name draw records by `first_instance`, so shaders use
  `ERHE_PRIMITIVE_INDEX` (doc/erhe/graphics.md "Draw records").

### Added

- `erhe::dataformat`: `get_vertex_format_signature()`.
- `erhe::primitive`: `vertex_encoding_version` (`mesh_optimizer.hpp`).

- `erhe::primitive`: `Position_quantization` and `get_position_quantization()`
  (moved from `erhe::scene_renderer`); `Buffer_mesh::position_quantization`
  and `Buffer_mesh::texcoord_quantization`, the affines a build encoded the
  mesh with, which the decoders read.

- `erhe::primitive`: `Triangle_soup_source::references_file()` (virtual,
  false by default), `Primitive::rebind_triangle_soup_source()`,
  `Primitive::detach_triangle_soup_source()`,
  `Primitive_shape::detach_triangle_soup_source()`.
- `erhe::gltf`: `Gltf_export_arguments::written_layout`
  (`Gltf_export_written_layout`, `Gltf_export_written_primitive`,
  `Gltf_export_written_image`), `rebind_sources_to_written_file()`
  (`Gltf_rebind_result`), `is_same_file()`.

- `erhe::file`: `Positional_file_reader` (lock-free parallel positional reads through one handle).
- `erhe::primitive`: `Primitive(source, counts)` / `Primitive_shape(source, counts)`
  / `Primitive_render_shape(source, counts)` make a shape whose payload is
  read later; `Primitive::load_payload()`, `Primitive_shape::load_payload()`,
  `Primitive_shape::is_payload_pending()`; `Triangle_soup_source::load_prepared_data()`
  (virtual, null by default).
- `erhe::gltf`: `Gltf_parse_arguments::mesh_payload` (`Mesh_payload_load`:
  `in_parse` default, `deferred`), `Gltf_data::prepared_statistics`,
  `Gltf_data::source_files`, `finish_prepared_mesh_statistics()`,
  `close_source_files()`.

- `erhe::gltf`: `Gltf_image_source` can be file-backed (`file_path`,
  `file_offset`, `byte_length`, `file_size`, `file_write_time`) with
  `is_file_backed()`, `get_byte_count()` and `read()`; `parse_gltf` gives
  every image loaded from a file such a source and keeps no image bytes.
  `export_gltf()` returns an empty string when a source cannot be read.

- `erhe::utility`: `Memory_budget`, `Memory_budget_lease` and
  `Memory_budget_kind` - a thread-safe byte budget handing out move-only
  leases (one request larger than the capacity is granted while nothing else
  is leased), with per-kind statistics.
- `erhe::graphics`: `Buffer_transfer_queue::hold_until_drained(Memory_budget_lease&&)`
  keeps a lease until the watermark has passed every upload enqueued before
  the call.
- `erhe::gltf`: `Gltf_image_residency::set_memory_budget()` shares a
  `Memory_budget` for the decoded image bytes; `schedule_decodes()` takes a
  default argument.

- `erhe::file`: `write_file_via_temp(path, text)` and
  `write_file_via_temp(path, write)`: write through a temporary file in the
  target directory and replace the target with one rename only when the
  write completed; a failed write leaves the old file unchanged.
- `erhe::primitive`: `Triangle_soup_source`, `Triangle_soup_counts`,
  `Triangle_soup_reload_statistics` / `get_triangle_soup_reload_statistics()`;
  `Primitive_shape::set_triangle_soup_source()`, `get_triangle_soup_source()`,
  `release_triangle_soup()`, `is_triangle_soup_released()`,
  `has_triangle_soup()`, `get_triangle_soup_counts()`,
  `shares_resident_triangle_soup()`; `Primitive::release_triangle_soup()`;
  `Shape_memory_statistics::triangle_soup_released_count`;
  `Buffer_mesh::release_buffer_allocations()`.
- `erhe::raytrace`: `IGeometry::release_buffers()`.
- `erhe::gltf`: `Triangle_soup_reload` and
  `Gltf_parse_arguments::triangle_soup_reload` (record per-primitive reload
  sources).

- `erhe::graphics`: capped upload staging (doc/erhe/ring_buffer_memory.md
  "Upload staging cap"): `Device::allocate_upload_staging(target,
  byte_count, capacity_byte_count)` (an empty range when there is no room;
  a request larger than the cap gets a one-shot buffer while nothing else
  is staged), `Device::get_upload_staging_usage()` and
  `Upload_staging_usage`, `Ring_buffer_pool::allocate_upload_staging()` /
  `get_upload_staging_usage()`, and `Command_buffer::try_upload_to_buffer()`
  (`upload_to_buffer` staged through the cap; false when it must wait).
- `erhe::graphics`: raw block KTX2 support (`ktx2_container.hpp`:
  `Ktx2_container`, `parse_ktx2()`, `write_ktx2_raw_blocks()` with
  `Ktx2_raw_blocks_image` and `Ktx2_supercompression` (none / zstd),
  `transcode_ktx2_basis_to_astc_4x4()` with `Ktx2_color_space`,
  `is_ktx2_basis()`, `is_ktx2_raw_blocks()`); `Image_loader_ktx2` opens raw
  ASTC 4x4 KTX2 files (passed through, or decoded to RGBA8 with the ARM
  astc-encoder decoder) selected by the new `Astc_support` argument of
  `Image_loader::open()` / `Image_loader_ktx2::open()`;
  `Image_info::decode_path` (`Image_decode_path`).
- `erhe::dataformat`: `is_srgb()`.
- `erhe::gltf`: "Save for Quest" textures: `Gltf_texture_export`,
  `Gltf_export_arguments::texture_export` / `texture_statistics`,
  `Gltf_export_texture_statistics`; the `ERHE_texture_ktx2` asset-root
  extension (written and parsed); `Gltf_device_options::astc_support`,
  `Gltf_image_residency::astc_support`,
  `Gltf_image_residency::get_decode_path_counts()` and
  `Gltf_image_decode_path_counts`.
- `erhe::gltf`: `Gltf_image_residency::schedule_decodes()`,
  `is_decode_idle()`, `get_pending_byte_high_water()`,
  `default_max_decoded_bytes_in_flight` and the decode settings
  `transcode_format_preference`, `decode_executor`, `parallel_decode`;
  `Gltf_decoded_image::linear` and `decoded`;
  `Image_transfer::default_staging_capacity_byte_count` and
  `get_staging_capacity_byte_count()`.
- `erhe::graphics`: `Buffer_memory_statistics` and
  `Buffer::get_memory_statistics()` (process-wide live buffer capacity by
  creation usage: acceleration structure storage, acceleration structure
  build input, vertex / index, other); `Texture::get_estimated_byte_count()`;
  `Memory_budget::device_local_block_bytes` and
  `device_local_allocation_bytes` (Vulkan: VMA blocks and allocations in
  device-local heaps over every allocator);
  `Device_debug_state::ring_buffer_capacity_bytes` and
  `Ring_buffer_pool_usage::capacity_bytes`.
- `erhe::raytrace`: `Geometry_memory_statistics` and
  `IGeometry::get_memory_statistics()` (BVH nodes, primitive ids,
  precomputed triangles, a supplied BVH not yet consumed; the bvh backend
  reports them, the others zeros).
- `erhe::primitive`: `Shape_memory_statistics`,
  `Primitive_shape::add_memory_statistics()` and
  `Primitive_raytrace::add_memory_statistics()` (CPU bytes of a shape's
  soup, element mappings, prepared data, stored BVH and raytraces; shared
  objects counted once through a caller-owned set).

- `erhe::primitive`: `Primitive_shape::has_stored_bvh()`,
  `mark_prepared_data_stale()`, `is_prepared_data_current()`,
  `Prepared_primitive_export::retained_core`, `get_export_core()` and
  `Prepared_bvh_source::stored`. The prepared-data constructors of
  `Primitive`, `Primitive_render_shape` and `Primitive_shape` now take
  `std::shared_ptr<Prepared_primitive_data>` (was a pointer to const) and move
  its `bvh` into the shape; `Primitive_raytrace`'s soup constructor takes
  `std::unique_ptr<erhe::raytrace::Prebuilt_bvh>`. `get_geometry()` of a
  prepared shape uses the stored colocation, and
  `prepare_primitive_for_export()` retains a shape whose prepared data is
  current.
- `erhe::scene`: `Mesh::begin_optimized_variant_edit()` marks the
  primitive's prepared data stale; the `Mesh_primitive` count properties fall
  back to the soup counts while there is no `Geometry`.
- `erhe::gltf`: `Gltf_export_prepared_statistics::bvh_stored_count` and
  `retained_count`.

- `erhe::primitive`: `Primitive::make_prepared_meshes()` and
  `Primitive::is_prepared()` (the load build of a primitive with prepared
  mesh data), `Primitive_shape::make_prepared_raytrace()` and
  `Primitive_raytrace::get_bvh_source()`. The soup constructor of
  `Primitive_raytrace` is now
  `Primitive_raytrace(const Triangle_soup&, const erhe::raytrace::Prebuilt_bvh*)`
  (was `Primitive_raytrace(Triangle_soup&)`): it takes an optional stored
  BVH and maps triangle t to facet t.
- `erhe::primitive`: `Prepared_primitive_data`, `validate_prepared_core()`
  and `validate_vertex_points()` (`prepared_primitive.hpp`); the
  constructors `Primitive(soup, prepared_data)`,
  `Primitive_render_shape(soup, prepared_data)`,
  `Primitive_shape(soup, prepared_data)` and
  `Primitive_shape::get_prepared_data()`.
- `erhe::gltf`: `Gltf_parse_arguments::prepared_mesh_data_use`
  (`Prepared_mesh_data_use::use` by default) and
  `Gltf_data::prepared_mesh_statistics` (`Gltf_prepared_mesh_statistics`):
  the parse validates and attaches `ERHE_mesh_optimized` /
  `ERHE_mesh_topology` / `ERHE_raytrace_bvh` payloads.

- `erhe::primitive`: `prepare_primitive_for_export()`,
  `Prepared_primitive_export`, `Prepare_primitive_result`,
  `Prepared_bvh_source`, `compute_mesh_content_hash()` and
  `get_float_positions()` (`erhe_primitive/prepared_primitive.hpp`): the
  glTF prepared core and its `ERHE_mesh_optimized` / `ERHE_mesh_topology` /
  `ERHE_raytrace_bvh` payloads.
- `erhe::raytrace`: `hash_triangle_positions()`, `compute_triangle_hash()`
  and `bvh_v2_traversal_stack_size` (`prebuilt_bvh.hpp`).
- `erhe::gltf`: `Gltf_export_arguments::prepared_mesh_data`
  (`Prepared_mesh_data::none` by default), `executor` and
  `prepared_statistics` (`Gltf_export_prepared_statistics`).

- `erhe::raytrace`: BVH export and import (`erhe_raytrace/prebuilt_bvh.hpp`):
  `Prebuilt_bvh`, `Bvh_node_record`, `Bvh_build_quality`, `Bvh_source`,
  `get_prebuilt_bvh_backend()`, `get_prebuilt_bvh_library()`,
  `validate_prebuilt_bvh()`, and the `IGeometry` virtuals
  `get_triangle_hash()`, `get_triangle_count()`, `export_bvh()`,
  `set_prebuilt_bvh()`, `get_bvh_source()` and
  `get_prebuilt_bvh_rejection()` (implemented by the `bvh` backend; the other
  backends report them not available). The `bvh` backend's `cache/bvh`
  directory key is now the pinned `GIT_TAG` (`ERHE_BVH_LIBRARY_TAG`) instead
  of `git rev-parse` of the checkout.

- `erhe::primitive`: `Vertex_colocation`, the overload
  `mesh_from_triangle_soup(const Triangle_soup&, GEO::Mesh&, Element_mappings&, Vertex_colocation&)`
  that also returns the colocation the colocate path used, and
  `mesh_from_triangle_soup(const Triangle_soup&, std::span<const uint32_t> vertex_points, uint32_t point_count, GEO::Mesh&, Element_mappings&) -> bool`,
  which builds the mesh from a supplied colocation without `colocate()` or
  the geogram lock and returns false on invalid input.

- `erhe::graphics`: `Gpu_timer_result` (nanoseconds + recording frame id),
  `Gpu_timer::last_result_with_frame_id()` and the allocation-free
  `Gpu_timer::for_each_gpu_timer(const std::function<void(Gpu_timer&)>&)`
  (walks the live timers under the registry lock).
- `erhe::graphics`: `Device_info::workaround_view_mipmap_generation_ignores_view_min_level`,
  set by the OpenGL backend for the AMD driver on Windows: there
  `Blit_command_encoder::generate_mipmaps()` builds a texture view's mipmaps
  with one blit per level and layer; every other driver keeps
  `glGenerateTextureMipmap()`.
- `erhe::xr`: head pose override for scripted runs:
  `Headset::set_head_pose_override(const std::optional<Pose>&)` and
  `Xr_session::set_head_pose_override()` / `get_head_pose_override()`.
  While set, the head is posed from the given stage-space pose and the
  views keep the runtime's eye offsets and fovs (located in the view
  space and composed with the override) in `get_headset_pose()`,
  `locate_views()`, `render()` / `render_multiview()` and the submitted
  projection layer; `Frame_timing::head_pose_override` carries the value
  of each waited frame to its render side.
- `erhe::xr`: `Xr_session::request_display_refresh_rate(float)`,
  `get_display_refresh_rate()` and `get_display_refresh_rates()` (the
  offered rates, enumerated and logged with the current rate when the
  session begins) (XR_FB_display_refresh_rate);
  `Xr_instance::xrEnumerateDisplayRefreshRatesFB`,
  `xrGetDisplayRefreshRateFB` and `xrRequestDisplayRefreshRateFB`.
- `erhe::xr`: `Pose` moved to its own header `erhe_xr/pose.hpp` (included
  by `xr.hpp` and `frame_timing.hpp`), available in every
  `ERHE_XR_LIBRARY` configuration.
- `erhe::scene_renderer`: occlusion culling (doc/plans/occlusion_culling.md
  3b): `Retired_occlusion_counts::culled`, `Occlusion_view_check::culled`,
  `Cull_classification_check::occlusion_culled_count`,
  `Draw_occlusion_parameters::eye`, `Draw_list_renderer::get_color_view_eye()`,
  `Gpu_cull_classifier::get_view_occlusion_header_offset()`,
  `get_occlusion_eye_offset()`, `occlusion_indices_per_entry`.
- `erhe::scene_renderer`: the raster occlusion test (doc/plans/occlusion_culling.md,
  doc/erhe/draw_list_renderer.md Q6 "Occlusion test"): `Occlusion_culling`,
  `Draw_list_scene::add_occlusion_view()`, `draw_occlusion()`
  (`Draw_occlusion_parameters`), `get_retired_occlusion_counts()`
  (`Retired_occlusion_counts`), `record_frame_end()`,
  `request_occlusion_check()` / `poll_occlusion_check()` (`Occlusion_check`,
  `Occlusion_view_check`, `Occlusion_check_entry`),
  `get_occlusion_culling()`; `Cull_frame_registry::add_frame_end_scene()` and
  `record_frame_end()`; `Draw_list_renderer::get_color_clip_from_world()` and
  `add_color_occlusion_view()`; `Gpu_cull_classifier::is_occlusion_supported()`,
  the occlusion pipeline accessors and binding point constants.
- `erhe::graphics`: `Device_info::fragment_stores_and_atomics` (Vulkan
  enables `fragmentStoresAndAtomics` when the device has it).
- `erhe::scene_renderer`: GPU-generated commands for the draw-list color
  passes (doc/erhe/draw_list_renderer.md Q6 "GPU command generation"):
  `Color_command_selection`, `Command_selection_id`, `Gpu_command_key`,
  `Draw_list_scene::add_color_command_selection()` and
  `get_retired_gpu_color_counts()` (`Retired_gpu_color_counts`),
  `Draw_color_parameters::command_selection`,
  `Draw_list_renderer::add_color_command_selection()` and
  `Draw_list_renderer::Render_parameters::command_selection`,
  `Draw_statistics::gpu_generated`, `Cull_classification_check::
  color_job_count`, `Gpu_cull_classifier::max_job_filters` (the job table
  carries a filter index per job and a filter table instead of one filter).
- `erhe::graphics`: `Base_render_pipeline::get_pipeline_for()` takes an
  optional `Indirect_count_draw_support` that requests a variant able to
  draw prepared indirect count draws whatever the base pipeline declares;
  `erhe::scene_renderer::Draw_list_scene::draw_color()` requests it for the
  draw-list color pipelines.
- `erhe::scene_renderer`: `Cull_frame_registry::add_record_scene()` and
  `Cull_frame_registry::release_frame_records()` (render phase, once per
  frame after the frame's last draw-list pass; an application drawing
  draw lists calls it), and `Draw_list_scene::release_frame_records()`.
  The color passes of the draw lists (`Draw_list_scene::draw_color()`) bind
  the frame's shared records like the shadow passes under
  `Record_indexing::first_instance` (doc/erhe/draw_list_renderer.md "Record
  upload").
- `erhe::graphics`: explicit per-resource barriers (doc/erhe/graphics.md
  "Resource barriers"): `Command_buffer::texture_barrier(texture, [range,]
  usage_before, usage_after[, scope])` and
  `Command_buffer::buffer_barrier(buffer, usage_before, usage_after[, scope])`;
  `Buffer_usage_flag_bit` / `Buffer_usage_flag_bit_mask` (the accesses of a
  buffer barrier side); the stage qualifier bits `vertex_shader`,
  `fragment_shader`, `compute_shader` (and the `shader_stages` mask) in
  `Image_usage_flag_bit(_mask)` and `Buffer_usage_flag_bit(_mask)`; the
  `discard` modifier in `Image_usage_flag_bit(_mask)` (usage_before only:
  drop the contents, still order the named earlier accesses);
  `Barrier_scope` (`command_buffer`, `earlier_submissions`) with `c_str()`;
  `Texture_subresource_range`.
- `erhe::graphics`: present calibration cache (`present_calibration_cache.hpp`,
  doc/erhe/graphics.md "Present calibration cache"): `Present_calibration_key`
  and `query_present_calibration_key()` (Apple: CGDisplay identity and mode,
  NSScreen refresh range, window mode, OS build, GPU name, `Present_path`,
  `c_present_calibration_version`), `get_present_calibration_key_hash()`,
  `make_present_calibration_cache_path()`,
  `read_present_calibration_cache_file()` /
  `write_present_calibration_cache_file()` (`Present_calibration_cache_entry`),
  `Present_calibration_store` (with this session's calibrations per key),
  `Present_calibration_key_query`, `Present_calibration_state`,
  `Present_calibration_summary` and `Present_calibration_source`;
  `Device::get_present_calibration_state()`,
  `get_present_calibration_summary()`,
  `get_present_request_offset()`, `begin_present_calibration()`,
  `finish_present_calibration()`, `refresh_present_calibration_for_display()`
  and `remove_present_calibration_cache_entry()`; `Graphics_config` version 3
  field `present_calibration_cache` (environment override
  `ERHE_PRESENT_CALIBRATION_CACHE=0`);
  `Display_refresh_resolver::notify_presentation()`.
- `erhe::frame_pacing`: `Frame_pacer_tunables::deadline_ceiling_sustain_samples`,
  `Frame_pacer_tunables::deadline_ceiling_tolerance`,
  `Frame_pacer::is_deadline_offset_ceiling_sustained()`,
  `Frame_pacer::get_tunables()` and
  `Frame_pacing_observer::set_deadline_ceiling_callback()`.

- `erhe::graphics`: `Device::get_debug_state()` returns a
  `Device_debug_state` (open and unretired frames, uncompleted command
  buffers, queued completion handlers, ring buffer count, busy ring buffers
  and their bytes in use) for test isolation checks;
  `Ring_buffer_pool::get_usage()` (`Ring_buffer_pool_usage`) and
  `Ring_buffer::get_bytes_in_use()` feed it.
- `erhe::graphics`: `ERHE_GRAPHICS_API=metal4` selects the Metal 4 backend
  (`doc/erhe/metal4_backend.md`), which defines `ERHE_GRAPHICS_API_METAL4` in
  addition to the Metal family define `ERHE_GRAPHICS_API_METAL`.
- `erhe::graphics`: GPU-counted multi-draws prepared in a compute pass:
  `Indirect_count_draws` (`indirect_count_draws.hpp`, with
  `Indirect_count_draws_prepare_info`, `Indirect_count_draw_job`,
  `Indirect_count_index_buffer`, `Indirect_count_draw_set`),
  `Compute_command_encoder::prepare_indexed_indirect_count_draws()`,
  `Render_command_encoder::multi_draw_indexed_primitives_indirect_count(set,
  job_index)`, `Indirect_count_draw_support` and
  `Base_render_pipeline_create_info::indirect_count_draws`. Metal now reports
  `Device_info::use_draw_indirect_count` (an indirect command buffer encoded
  by a kernel); its older `multi_draw_indexed_primitives_indirect_count()`
  overload stays fatal (doc/erhe/graphics.md "Draw count from the GPU"). A
  frame may prepare any number of sets; `Indirect_count_draw_set::sequence`
  names the prepare and a set is drawn before the next prepare. The
  `erhe::scene_renderer` GPU culling prepares one set per shadow pass
  (`Draw_list_scene::prepare_gpu_shadow_pass()`).

- `erhe::scene_renderer`: `Shadow_fit_cache`, `Caster_extents_identity`
  (`caster_extents_identity.hpp`), `Shadow_bounds::extents_identity`,
  `Shadow_renderer::Render_parameters::fit_cache`, the trailing
  `fit_cache` / `caster_extents_identity` parameters of
  `Light_projections::apply()` (defaulted) and
  `Light_projections::light_fit_reused`, `light_fit_serials`,
  `fit_reused_count`: the per-light caster vertex rounding is reused while
  the light's view origin and the prepared caster extents are unchanged
  (doc/erhe/shadows.md "Shadow fit cache").

- `erhe::primitive`: `normalize_joint_weights()`,
  `quantize_joint_weights_unorm8()` and
  `include_vertex_in_joint_bounding_boxes()`: the builder writes `WEIGHTS_0`
  normalized (unorm8 summing to exactly 255) and builds the joint boxes from
  the same weights, so they bound every built skinned vertex
  (doc/erhe/primitive.md "Skinning weights").

- `erhe::scene_renderer`: `Draw_list_content_changes::skinned_changed_bounds`
  (old and new posed bounds of skinned shadow casters whose captured joint
  poses changed) and `Draw_list_object::published_pose`,
  `published_pose_aabb`, `has_published_pose`; the shadow map cache keeps
  passes in scenes with skinned objects (doc/erhe/shadows.md "Shadow map
  cache").

- `erhe::scene_renderer`: `Draw_list_content_changes` and
  `Draw_list_scene::get_published_content_changes()`: per publish, the old
  and new bounds of the shadow-list entries changed since the publish
  before it (or `unbounded`), the flags changes with their old and new bits,
  its base serial and the scene's process-unique
  instance; `Draw_list_scene::get_cull_view_tests()`; `Cull_view_kind` and
  `Cull_view_tests` (namespace scope, were private to `Draw_list_scene`)
  with `Cull_view_tests::is_same()`; `cull_lanes::reaches_view()` and
  `cull_lanes::View_reach_scratch`; `Draw_list_object::world_aabb` and
  `has_world_aabb`; `Shadow_draw_statistics::shadow_map_2d_draw_counts` and
  `point_cube_face_draw_counts` (cumulative draws per pass).

- `erhe::graphics`: `present_calibration_runner.hpp`
  (`IPresent_calibration_presenter`, `Present_calibration_probe_feedback`,
  `run_present_calibration`, `present_calibration_wait_until`): the
  backend-neutral present calibration (grid fit, request window search,
  deadline split) over clear-only probes a backend presents; Metal tier M
  and Vulkan tier W through `VK_GOOGLE_display_timing` (MoltenVK) run it at
  device init (doc/erhe/vulkan_backend.md "Surface and swapchain").
- `erhe::graphics`: `Display_presentation::update_granularity` (Metal: the
  window screen's NSScreen `displayUpdateGranularity`; 4.167 ms on a
  ProMotion panel, which refreshes only at its own ticks of that spacing) and
  `erhe::frame_pacing`: `Frame_pacer_tunables::refresh_granularity`: on a
  variable refresh display with a granularity the pacer locks its slots
  onto the display's ticks (doc/frame_pacing/algorithm.md deviation 26,
  claim C32).
- `erhe::graphics`: `Display_presentation` (`kind`, `minimum_refresh_interval`,
  `maximum_refresh_interval`, `fullscreen`),
  `Display_refresh_resolver::set_window_presentation`,
  `get_presentation`, `set_presentation_callback` and
  `Display_refresh_report::presentation`: how the window's display presents
  (fixed grid, or a variable refresh rate display driven fullscreen; Metal
  reads the NSScreen refresh-interval range, doc/erhe/metal_backend.md
  "Frame pacing").
- `erhe::frame_pacing`: `Present_display_kind` (`fixed_grid`,
  `variable_refresh`), `Frame_pacer_tunables::display` and
  `max_refresh_interval`, and
  `Frame_pacing_observer::set_display(Present_display_kind, double)`: the
  host declares an Adaptive-Sync display driven directly, which has no grid
  to track (doc/frame_pacing/algorithm.md deviation 24, claim C30).
- `erhe::primitive`: `Blending_class` (`opaque`, `alpha_tested`,
  `translucent`), `get_blending_class(Material_blending_mode)`,
  `get_blending_class(const Material*)` (no material classifies as opaque),
  `blending_class_draw_order` and `c_str(Blending_class)`: the draw order
  class of a blending mode (doc/erhe/primitive.md "Blending classes").
- `erhe::scene_renderer`: `Blending_mode_policy::alpha_tested_primitives_only`,
  `Draw_blending_selection::alpha_tested_only`,
  `is_blending_class_selected(Draw_blending_selection, Blending_class)` and
  `Shader_key::get_blending_class()` (doc/erhe/scene_renderer.md "Draw order
  of the blending classes").

- `erhe::scene_renderer`: `Light_cluster_binner` input layout adds per-slot
  spot cones (axis * sin, cot) after the light spheres; the lists cone-cull
  closed clusters (doc/erhe/scene_renderer.md `Light_cluster_binner`).
- `erhe::scene_renderer`: selectable specular anti-aliasing of the
  anisotropic BRDF (doc/erhe/scene_renderer.md "Specular anti-aliasing").
  `Specular_anti_aliasing` (`off`, `normal_kernel`, `finite_difference`)
  and the `Shader_int::SPECULAR_ANTI_ALIASING` variant axis;
  `set_specular_anti_aliasing_axis(Shader_key&, Specular_anti_aliasing)`
  sets it on the keys whose shader calls `anisotropic_brdf()` only (after
  the force masks, in the draw, prewarm and draw-list paths alike), and
  `get_specular_anti_aliasing(const Shader_key&)` reads it. New `specular_anti_aliasing` members (default
  `normal_kernel`) in `Forward_renderer::Render_parameters`,
  `Draw_parameters` and `Prewarm_parameters`,
  `Draw_list_renderer::Render_parameters` and `Color_environment`. Shader
  side: `anisotropic_brdf()` (`erhe_bxdf.glsl`) takes the kernel roughness
  as a new last argument and no longer takes a derivative;
  `specular_anti_aliasing()` is replaced by
  `specular_aa_kernel_from_normal_derivatives()`,
  `specular_aa_begin_fragment()` and `specular_aa_light_kernel()`. The
  default changes the look: `finite_difference` reproduces the previous
  kernel (8-bit identical with one directional light on the default scene;
  with a spot or point light it differs along silhouettes and the `N_dot_L`
  terminator, where the previous derivative was undefined), while
  `normal_kernel` adds no kernel on flat-shaded faces.

- `erhe::scene_renderer`: GPU light binning (doc/plans/clustered_lights.md
  phase 3). `light_cluster_gpu_binner.hpp`: `Light_cluster_gpu_binner`
  (the compute pipeline of `res/shaders/light_cluster_bin.comp`;
  `dispatch()`, `is_supported()`, `request_check()` / `poll_check()`),
  `Light_cluster_binning` (`cpu`, `gpu`) and `c_str()`,
  `Light_cluster_stats`, `Light_cluster_view_state` (`retire()`,
  `set_cpu_stats()`, `get_stats()`, `get_gpu_time_ns()`),
  `Light_view_binning`, `Light_cluster_check`, `Light_cluster_view_check`,
  `Light_cluster_mismatch`, `Light_cluster_check_state`.
  `Light_cluster_binner::prepare()` / `bin_prepared()` (the grid and fp32
  inputs, then the CPU tests; `bin()` does both), `get_capacity()`,
  `get_binned_light_count()`, `get_input_byte_count()` / `write_inputs()`,
  `get_fixed_list_stride()` / `get_fixed_byte_count()`,
  `get_cluster_category_counts()`; `c_light_cluster_bin_header_uvec4_count`.

- `erhe::scene_renderer`: clustered light shading (doc/plans/clustered_lights.md
  phase 2). `Light_shading` (`loops`, `clustered`);
  `Light_count_limits::max_light_count` (bound on the shaded total) and
  `Light_count_limits::light_shading`, carried by
  `Light_layer_partition::light_shading` (which gains a defaulted
  `operator==`); `set_light_partition_axes(Shader_key&, partition)` sets
  the six `LIGHT_COUNT_*` axes (loops) or the new `Shader_bool::
  LIGHT_SHADING_CLUSTERED` axis (clustered). `light_cluster.hpp`:
  `Light_cluster_config` (tile size, slice count, capacity),
  `Light_cluster_grid`, `Light_cluster_view`, `Light_cluster_binner`,
  `c_light_cluster_header_uint_count`, `get_single_cluster_byte_count()`,
  `write_single_cluster()`. `Light_view_buffers` and
  `Light_projections::view_buffers`; `Light_buffer::write_view()` writes a
  view's light array and cluster lists once per frame, which `update()`
  then binds. `Light_interface::light_cluster_block` (`light_cluster.data[]`,
  binding `light_cluster_buffer_binding_point` = 10) and
  `Light_interface::cluster_config`; the `cluster_*` grid fields of the
  `light_block` header. `Program_interface_config::light_cluster`.
  `Shadow_renderer::get_light_buffer()`. `get_cluster_slot_end()`; shader
  prototypes get the define `ERHE_LIGHT_CLUSTER_HEADER_UINT_COUNT`.

- `erhe::scene_renderer`: `Shadow_map_cache` and
  `Shadow_renderer::Render_parameters::map_cache`: `draw()` keeps the
  shadow passes (2D layers, cube faces) whose light, matrix and frame
  configuration are unchanged (doc/erhe/shadows.md "Shadow map cache");
  `Shadow_draw_statistics::shadow_map_2d_cached`, `point_cube_cached_faces`
  and `cached_pass_count` report them. `Draw_list_scene::
  get_published_content_serial()` and `has_published_skinned_objects()`.

- `erhe::math`: `Viewport::operator==` (defaulted).

- `erhe::frame_pacing`: `Clock_record` (claim C29): a cadence's clock
  samples with each frame's work; `Frame_pacer_tunables::clock_record_work_band`
  (1.5) - the upshift test judges N-1 at the clocks of a similar workload
  only. `Frame_pacer::get_upshift_state()` / `Upshift_state`: the upshift
  path's dwell, last stage time and the clocks N-1 is judged at
  (telemetry).

- `erhe::frame_pacing`: `Frame_time_record::gpu_idle_time` and
  `gpu_service_time()`: the GPU idle gap inside a frame's span (Vulkan: the
  frame bracket's gap between the upload submit and the presenting submit)
  and the span without it. `Frame_pacing_observer` passes `on_gpu_done` the
  service time as the GPU duration.

- `erhe::frame_pacing`: `Frame_time_record::release_wait_time(release)`
  (claim C28): the involuntary waits between a frame's release and its
  submit (FR5 clamp, join, acquire, fence). `Frame_pacing_observer` passes
  `on_gpu_done` the chain latency without them, and `Frame_pacer` keeps a
  cadence's latency window for the current visit only (a cadence change
  clears it and seeds a floor until the visit has `cold_start_seed_frames`
  samples).
- `erhe::frame_pacing`: `Frame_pacer_tunables::clock_settle_time` (claim
  C27): the clock records hold each cadence's steady clock - samples start
  that long after the cadence's first GPU end; frames delivered after their
  cadence was left and frames that ran a pre-warm are not recorded.
  `Deadline_offset_setup::upshift_prewarm` now applies without a calibrated
  deadline, and `Present_calibration::prewarm_seconds_per_iteration` is
  measured even when the calibration is not valid.
- `erhe::frame_pacing`: GPU pre-warm before an upshift (claim C26):
  `Schedule_decision::prewarm_seconds`, `Frame_pacer_tunables::upshift_prewarm_seconds`
  / `upshift_prewarm_frames` / `upshift_prewarm_clock` /
  `upshift_prewarm_max_frac`, `Deadline_offset_setup::upshift_prewarm`.
  `erhe::graphics`: `Device::set_present_prewarm()` /
  `get_present_prewarm()`, `Present_calibration::prewarm_seconds_per_iteration`
  (the Metal backend runs the pre-warm in tier M).
- `erhe::frame_pacing`: `Frame_time_record::gpu_clock_ratio` /
  `cpu_clock_ratio` and the matching arguments of `Frame_pacer::on_gpu_done`
  (0.0: not measured): the GPU's and the CPU's average active clock over a
  frame as a ratio of its maximum. The upshift test predicts the stage
  times at the next shorter cadence from the work at the clocks measured
  there (claim C25). The Metal backend measures them in tier M (IOReport).
- `erhe::graphics`: `Thermal_state`, `get_thermal_state()` and
  `start_thermal_state_logging()` (`thermal_state.hpp`): the machine's
  thermal state (Apple: `NSProcessInfo.thermalState`, unknown elsewhere),
  logged at device init and on every change.
- `erhe::graphics`: `Present_calibration` / `Present_calibration_sample`
  (`present_calibration.hpp`) and `Device::get_present_calibration()` /
  `set_present_calibration()`: the request window and deadline of a
  presentation path that honors target present times, measured at device
  init. The Metal backend measures them in tier M and requests its presents
  at the calibrated offset.
- `erhe::frame_pacing`: `Deadline_offset_setup` and
  `Frame_pacing_observer::set_deadline_offset()` (replacing
  `set_deadline_offset_source()`): the source plus a host-measured initial
  deadline offset and estimate ceiling for a given refresh period;
  `Frame_pacer_tunables::deadline_offset_max` (absolute ceiling, 0.0: the
  fraction of T).
- `erhe::frame_pacing`: `Present_queue_kind` (`dropping`, `fifo`) and
  `Frame_pacer_tunables::present_queue` / `Deadline_offset_setup::present_queue`:
  on a FIFO queue the in-flight anchor follows every in-flight target.
  `Frame_pacer_tunables::cadence_min_samples` (30): the predictive downshift
  now judges the current cadence by the stage times and pressure of the
  frames scheduled at it (cleared on each cadence change).
- `erhe::graphics`: `Device::is_present_target_time_honored()`: true when the
  presentation path displays an image at the first refresh at or after its
  target present time and takes it at its GPU end. The Metal backend
  resolves frame pacing tier M (`Frame_pacing_tier::full`: presented-handler
  feedback and present wait, `presentDrawable:atTime:` targets, the GPU frame
  bracket) for `frame_pacing_tier` "w", and returns true there; elsewhere
  false.
- `erhe::frame_pacing`: `Frame_pacer_tunables::upshift_quantile` (0.9): the
  upshift dwell allows that share's complement of its frames inside the
  hysteresis band below the next cadence's period. The in-flight anchor now
  follows the in-flight frames' target slots (bounded by their cadence),
  removing a slot-collision limit cycle after a cold-start miss.
- `erhe::frame_pacing`: `Deadline_offset_source` (`fixed`, `estimated`) and
  the `Frame_pacer_tunables` that drive the estimated presentation deadline
  offset (`deadline_offset_source`, `deadline_offset_max_frac`,
  `deadline_evidence_band`, `deadline_evidence_min_late`,
  `deadline_offset_decay`); `Frame_pacer::get_deadline_offset()`,
  `Frame_stats::deadline_offset`,
  `Frame_pacing_observer::set_deadline_offset_source()`. The FR5 wait id
  now also counts the images a deadline offset of one or more cadence
  periods holds in the queue (unchanged with the fixed 2.5 ms offset).
- `erhe::graphics`: `Blit_framebuffer_region` and
  `clip_blit_framebuffer_region()`. `Blit_command_encoder::blit_framebuffer`
  now has one contract on every backend: an unscaled copy of `source_size`
  texels at `source_origin` to `destination_origin`, clipped to both
  attachments, converting between their formats. Implemented on Metal (copy,
  or a draw between formats) and on Vulkan for offscreen render passes
  (`vkCmdBlitImage`); OpenGL passed `source_size` as the end coordinates and
  now uses the clipped rectangle.
- `erhe::graphics`: `Display_refresh_resolver::skip_calibration()`: ends the
  init calibration before it starts for a backend without one (logged at
  info level; `abort_calibration()` stays the failure path). The Metal
  backend now resolves frame pacing tier S (display-synced layer, three
  drawables) and stamps the acquire, fence wait and present records.
- `erhe::graphics`: `Device_info::texture_heap_size`, the descriptor count of
  the texture heap array: `max_texture_heap_size` bounded by the device's
  descriptor limits (Vulkan: `maxPerSetDescriptors`). `Texture_heap` caps its
  `max_textures` to it (it was required to be `<= max_texture_heap_size`).
- `erhe::graphics`: `Device_info::use_constant_alpha_color_blend_factors`:
  `Blending_factor::constant_alpha` / `one_minus_constant_alpha` are valid as
  rgb blend factors (Vulkan portability subset `constantAlphaColorBlendFactors`).

- `erhe::scene_renderer`: `Shader_debug::base_color` (37, "Base Color
  (resolved)"): the resolved base color the BxDF receives (material input
  source, factor, vertex color and base color texture multiplied), unlit.
- `erhe::graphics`: `Display_refresh_resolver` (`display_refresh_resolver.hpp`)
  with `Display_refresh_report`, `Display_refresh_source`,
  `Display_refresh_mode` and `Display_time_sample_source`, owned by `Device`
  (`Device::get_display_refresh_resolver()`,
  `Device::get_display_refresh_report()`): resolves the display refresh
  period the frame pacer runs on from the window's display mode, the
  swapchain timing query (`refreshDuration` / `refreshInterval`, re-read on
  a timing properties counter change) and an init calibration that measures
  the display with a dedicated clear-only swapchain at device init (free
  run, then plain FIFO; GPU frame end deltas), logging every source
  (`erhe.graphics.display_refresh`). `Device::set_display_refresh_duration_seconds`
  is the resolver's output, no longer set by the swapchain directly;
  `Device::is_display_refresh_calibrating()` reports a running calibration.
  `Display_refresh_policy` with `Display_refresh_resolver::set_policy()` and
  `parse_display_refresh_policy()`: the graphics config keys
  `display_refresh_source` (auto, window, vulkan, calibration, override) and
  `display_refresh_override_hz` choose the deciding source or an explicit
  rate (`Graphics_config` version 2). One warning whenever two known sources
  disagree beyond tolerance.
- `erhe::window`: `Context_window::get_display_refresh_period_seconds()`,
  `get_display_name()`, `register_display_changed_callback()` (SDL:
  called on `SDL_EVENT_WINDOW_DISPLAY_CHANGED` and the display mode change
  events) and `enumerate_displays()` with `Display_info`
  (`display_info.hpp`: every display, its current mode, the primary flag and
  which one hosts the window; logged when the SDL window opens). GLFW and
  the null window report nothing.

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
- `erhe::graphics`: `Device::capture_last_frame()` / `request_frame_capture()`
  on the OpenGL backend: an armed capture reads the default framebuffer's back
  buffer just before the buffer swap.
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

- `erhe::primitive`: `Primitive_shape::get_triangle_soup()` returns the soup
  by value (`std::shared_ptr<Triangle_soup>`) and reads a released soup back
  from its source.
- `erhe::primitive`: `Primitive_raytrace` frees its vertex / index copies
  after the commit when the backend does not read them, and maps a soup
  raytrace's facets by identity without a table.
- `erhe::gltf`: `parse_gltf()` no longer decodes images; it records each
  image's encoded stream and `Gltf_image_residency` decodes / transcodes on
  executor workers ahead of the upload under a decoded-bytes budget.
  `process_next_image()` and `drain()` take an optional
  `max_decoded_bytes_in_flight`; `get_pending_byte_count()` reports the
  decoded bytes in flight. `parse_gltf()` and `scan_gltf()` stream the file
  instead of reading it whole.
- `erhe::gltf`: `Image_transfer` takes an optional staging capacity
  (`Image_transfer(device, mode, staging_capacity_byte_count)`):
  `blocking_drain` sizes its private ring by it, and `frame_recording`
  stages through `Device::allocate_upload_staging` with it instead of the
  spilling device ring, so `upload_into_frame()` also returns
  `budget_exhausted` when the staging has no room.
- `erhe::graphics`: `Buffer_transfer_queue::flush_budgeted()` takes a
  `staging_capacity_byte_count` and stages through
  `Command_buffer::try_upload_to_buffer()`; entries that find no staging room
  stay queued in order for the next drain.
- `erhe::scene_renderer`: `Mesh_memory::flush_budgeted()` takes a
  `staging_capacity_byte_count`, passed to the loader queue's drain.
- `erhe::graphics`: Vulkan `Device::get_memory_budget()` no longer adds the
  device-address allocator's usage when VK_EXT_memory_budget is enabled:
  each allocator then reports the driver's usage of the whole process, so
  the sum counted it twice whenever ray tracing created the second
  allocator.

- `erhe::scene_renderer`: `Draw_list_scene` reads a retired frame's
  occlusion words back on the CPU only when an occlusion check
  (`request_occlusion_check()`) recorded that frame, so
  `get_retired_occlusion_counts()` updates only for checked frames (empty
  before the first). The per-frame readback cost ~4.5 ms of render thread
  time on Bistro.
- `erhe::scene_renderer`: the GPU classification consumes the previous
  frame's occlusion result (doc/plans/occlusion_culling.md 3b): an occlusion
  view's mask bit is cleared for an entry the same view found occluded in
  the frame before, so the GPU-generated color commands and the passes'
  counts include the occlusion term. `cull_classify.comp` binds the
  occlusion words as `cull_occlusion` / `cull_occlusion_previous`
  (`Gpu_cull_classifier::occlusion_words_binding_point`,
  `occlusion_previous_words_binding_point`, replacing
  `occlusion_test_binding_point` / `occlusion_result_binding_point`); the
  occlusion test draws the three camera-facing faces of each box, indexed
  (`occlusion_vertices_per_entry` is 7).
- `erhe::scene_renderer`: `Draw_list_scene::publish()` takes the frame's
  `Occlusion_culling` after the `Cull_classification`;
  `Gpu_cull_classifier::vec4s_per_view` is 10 (the near plane and flag of an
  occlusion view), and `cull_classify.comp` binds two more storage blocks
  (the occlusion test and result words).
- `erhe::graphics` (Metal and Metal 4): fragment functions read every
  dedicated (non-heap) sampler of their bind group layout from one argument
  buffer at `[[buffer(15)]]` (descriptor set 6) instead of
  `[[texture(N)]]` / `[[sampler(M)]]`; vertex and compute functions keep the
  direct bindings. `Render_command_encoder::set_sampled_image()` is
  unchanged for callers. A layout with fragment-stage dedicated samplers may
  not declare a buffer at binding 15 (verified). Every render pipeline can
  therefore be created with `Indirect_count_draw_support::enabled` on Metal
  (doc/erhe/metal_backend.md "Two Sampler Binding Paths").
- `erhe::graphics`: on Vulkan the render pass begin and end barriers honour
  the stage qualifiers of an attachment's `usage_before` / `usage_after` as
  `Command_buffer::texture_barrier` does (`sampled | compute_shader` orders
  compute readers; an unqualified `sampled` covers every shader stage, was
  the fragment stage only); `Command_buffer::cmd_texture_barrier` maps its
  usages the same way.
- `erhe::graphics`: `Device::get_present_calibration()` returns a copy (the
  calibration changes at run time on a display change);
  `Device::set_present_calibration()` is replaced by
  `begin_present_calibration()` / `finish_present_calibration()`;
  `Display_refresh_resolver::set_window_presentation()` returns whether the
  presentation changed.
- `erhe::primitive`: the primitive builder normalizes `WEIGHTS_0` (skinned
  meshes whose authored weights did not sum to one, or whose unorm8
  rounding did not, render slightly differently); the joint boxes include a
  vertex without joint values in joint 0's box;
  `encode_implicit_sum_joint_weights()` reconstructs a fourth weight of
  exactly zero for vertices with fewer than four influences.

- `erhe::scene_renderer`: `Draw_list_scene::publish()` takes the frame's
  captured joint poses (`std::span<const Joint_pose>`).

- `erhe::scene_renderer`: `Shadow_map_cache` keeps a pass through draw-list
  changes whose old and new bounds miss the pass's culling view (the
  content serial left its configuration; doc/erhe/shadows.md "Shadow map
  cache").

- `erhe::frame_pacing`: `Frame_pacing_observer::set_display(Present_display_kind,
  double max_refresh_interval, double refresh_granularity)` takes the
  display's update granularity (0 = continuous; claim C32).

- `erhe::scene_renderer`: the 416-byte light record is split into three
  std430 arrays (doc/erhe/scene_renderer.md "Light records"):
  - `Light_struct` / GLSL `Light` (`light_array.lights[slot]`, binding 9,
    48 bytes): keeps `position_and_inner_spot_cos`,
    `direction_and_outer_spot_cos`, `radiance_and_range`.
  - New `Light_shadowed_struct` / GLSL `Light_shadowed`
    (`Light_interface::light_shadowed_struct`, block
    `light_shadowed_array_block`, `light_shadowed_array.lights[record]`,
    binding 11, `light_shadowed_array_buffer_binding_point`, 208 bytes):
    the three shading vec4s plus `view_origin`, `texture_from_view_relative`
    and `view_relative_from_texture` (moved from `Light_struct`) and
    `shadow_params`, which replaces `shadow_limits` (x: caster vertex
    rounding, was `shadow_limits.x`) and `shadow_index_packed` (y: 2D layer,
    was `.x`; z: cube index, was `.y`; w: distance rays valid as 1.0 / 0.0,
    was `.z`), all as floats.
  - New `Light_shadow_world_struct` / GLSL `Light_shadow_world`
    (`light_shadow_world_array_block`,
    `light_shadow_world_array.transforms[record]`, binding 12,
    `light_shadow_world_array_buffer_binding_point`, 128 bytes):
    `texture_from_world` and `world_from_texture` (moved from
    `Light_struct`).
  - `Light_struct::clip_from_world` is removed (no reader).
  The record index of a shadow-mapped slot is its 2D layer, or the 2D layer
  count plus its cube; GLSL `erhe_light_shadow_record(slot)`
  (`erhe_light_record.glsl`) computes it from the `light_block` counts.
  `sample_light_visibility()` takes the record index after the light
  index. `Light_block` gains `shadowed`, `shadow_world`, `shadowed_struct`
  and `shadow_world_struct`; `Light_view_buffers` gains `shadowed` and
  `shadow_worlds`; `Light_buffer_ranges` gains `shadowed` and
  `shadow_worlds`, and its `view_lights` / `view_clusters` pointers are
  replaced by `view_buffers`. `Light_projections::apply()` composes the
  view-relative matrices and the bias limits for shadow-mapped slots only.

- `erhe::scene_renderer`: `alpha_test` and `screen_door` primitives moved
  out of the translucent class into their own: they used to be taken by
  `Blending_mode_policy::translucent_primitives_only` /
  `Draw_blending_selection::translucent_only` and drawn with
  `color_blend_premultiplied`, and are now taken only by
  `alpha_tested_primitives_only` / `alpha_tested_only` and drawn with
  blending off. `allow_all` / `Draw_blending_selection::all` (was `both`)
  draw opaque, alpha-tested and translucent primitives in that order in
  `Forward_renderer::render()` and `Draw_list_scene::draw_color()`.
  `Draw_list_key::blending` is an `erhe::primitive::Blending_class`. A
  draw-list primitive without a material now classifies as opaque (it was
  translucent), as the bucket path already did, so it enters the opaque
  color lists and the shadow lists. `standard.frag` runs the alpha-test /
  screen-door discard before the ambient term and the light loops, and the
  lightmap, DDGI and shadow-compare reads after it select level 0
  explicitly (`textureLod`, `textureGrad` with zero gradients).

- `erhe::scene_renderer`: `Light_buffer::write_view()` takes a
  `Light_view_binning` (default: CPU binning, no stats): with
  `Light_cluster_binning::gpu`, a supported `Light_cluster_gpu_binner`, the
  frame's command buffer and a `Light_cluster_view_state`, the view's lists
  are binned by a compute dispatch into a `GPU_access` range at fixed
  per-cluster offsets. `Light_cluster_binner`'s tests are fp32 (the GPU
  binner's operations), over inputs composed in double and rounded once.

- `erhe::scene_renderer`: `Light_interface`'s constructor takes a
  `Light_cluster_config`. `Light_buffer_ranges` gains `clusters` (the
  single-cluster grid `update()` wrote) and `view_lights` /
  `view_clusters` (the view's buffers when `write_view()` wrote them);
  `bind_light_buffer()` also binds the cluster lists, so a bind group
  layout or shader prototype that names `light_array_block` also names
  `light_cluster_block`. `Light_set::resolve()` and
  `compute_light_layer_partition()` take the light array capacity
  (`Light_interface::max_light_count`) and stop handing out slots at the
  smaller of it and `Light_count_limits::max_light_count`;
  `Light_set::get_light_count_limits()` reports that bound.
  `Color_environment::operator==` compares the environment keys
  (`make_environment_key()`).

- `erhe::scene_renderer`: the lights moved out of `light_block` into
  `Light_interface::light_array_block`, a readonly storage block
  `light_array` (`light_array.lights[]`, binding
  `light_array_buffer_binding_point` = 9) holding the shaded lights only;
  `light_block` keeps the header (counts, ambient, shadow map sentinels,
  DDGI volume). `Light_buffer::update()` returns `Light_buffer_ranges`
  (`header`, `lights`, `release()`), and `bind_light_buffer()` takes it.
  A bind group layout or shader prototype that names `light_block` also
  names `light_array_block`.

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
- `erhe::graphics`: the OpenGL backend supports `format_8_vec4_bgra_unorm`
  and `format_8_vec4_bgra_srgb` textures (stored as `GL_RGBA8` /
  `GL_SRGB8_ALPHA8`, uploaded and read back with the `GL_BGRA` client
  format); `erhe::gl::convert_to_gl()` maps them to those internal formats.
  `Device::get_format_properties()` on OpenGL reports a format without an
  OpenGL internal format (`format_8_scalar_srgb`, `format_8_vec2_srgb`) as not
  supported; it aborted.
- `erhe::graphics`: `Blit_command_encoder::blit_framebuffer()` on OpenGL is no
  longer clipped by a scissor rectangle left by an earlier pass.
- `erhe::graphics`: `Gpu_timer` ranges may nest on every backend: explicit
  ranges may enclose each other and timed render passes. The OpenGL backend
  records a `GL_TIMESTAMP` query pair per range instead of one
  `GL_TIME_ELAPSED` query, which allowed one active timer at a time.
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

- `erhe::scene_renderer`: `Draw_list_scene::has_published_skinned_objects()`
  (skinned objects no longer disable the shadow map cache).
- `erhe::scene_renderer`: `Draw_list_scene::get_published_content_serial()`
  (replaced by `get_published_content_changes()`).
- `erhe::primitive`: `needs_translucent_pass()` (replaced by
  `get_blending_class()`).
- `erhe::scene_renderer`: `Draw_blending` and `c_str(Draw_blending)`
  (replaced by `erhe::primitive::Blending_class`).

- `erhe::scene_renderer`: `Light_view_buffers::overflow_cluster_count` and
  `max_list_length` (the binning stats are `Light_cluster_view_state::
  get_stats()`).

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
