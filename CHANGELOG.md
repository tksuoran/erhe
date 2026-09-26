# Changelog

All notable changes to the public API of the erhe libraries (`erhe::*`,
`src/erhe/`) are recorded here. The editor and the other executables are not
covered. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
the rule for adding entries is in `doc/README.md` ("Changelog").

## [Unreleased]

### Added

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

### Fixed

- `erhe::commands`: `Command_binding::c_type_strings` misses no `Type` any
  more (it lacked `Controller_axis` / `Controller_button`), and
  `Controller_button_binding::get_type()` returns `Type::Controller_button`
  instead of `Type::Xr_boolean`.

- `erhe::renderer`: a negative `Primitive_renderer::set_thickness()` (constant
  screen-space width) scaled with the viewport width and the camera field of
  view; it is now `-thickness` logical pixels times `View::pixel_scale`.
