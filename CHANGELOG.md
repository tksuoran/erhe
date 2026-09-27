# Changelog

All notable changes to the public API of the erhe libraries (`erhe::*`,
`src/erhe/`) are recorded here. The editor and the other executables are not
covered. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the rule for adding entries is in `doc/README.md` ("Changelog").

## [Unreleased]

### Added

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

### Changed

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
