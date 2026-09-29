# Changelog

All notable changes to the public API of the erhe libraries (`erhe::*`,
`src/erhe/`) are recorded here. The editor and the other executables are not
covered. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the rule for adding entries is in `doc/README.md` ("Changelog").

## [Unreleased]

### Added

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

### Changed

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

- `erhe::commands`: `Command_binding::c_type_strings` misses no `Type` any
  more (it lacked `Controller_axis` / `Controller_button`), and
  `Controller_button_binding::get_type()` returns `Type::Controller_button`
  instead of `Type::Xr_boolean`.

- `erhe::renderer`: a negative `Primitive_renderer::set_thickness()` (constant
  screen-space width) scaled with the viewport width and the camera field of
  view; it is now `-thickness` logical pixels times `View::pixel_scale`.
