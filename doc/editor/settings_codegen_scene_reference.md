# Editor settings, codegen, and scene serialization reference

Stability: stable

Reference map for three subsystems that are easy to lose track of: the editor
settings model, the `erhe_codegen` struct generator, and scene save / load. It
names files and fields rather than repeating their contents, so check that a
named file or field still exists before relying on it.

---

## 1. Editor settings

All editor settings are one generated struct, `Editor_settings_config`, defined in
`src/editor/config/definitions/editor_settings_config.py`. It is loaded from and
saved to `config/editor/editor_settings.json`.

### Load / save path

- Owner: `Editor_settings_store` (`src/editor/editor_settings_store.{hpp,cpp}`).
  Wrapped by `App_settings` (`src/editor/app_settings.{hpp,cpp}`).
- Load: `erhe::codegen::load_config<Editor_settings_config>(path, &upgraded)` at
  startup; codegen handles version migration.
- Save: per-frame `Editor_settings_store::update(allow_save)` runs registered
  "collect" callbacks (each subsystem writes its live state back into the struct),
  serializes, diffs against the last saved string, and writes only on change
  (so mouse-drags do not thrash the file). Subsystems register via
  `register_collect_callback([](Editor_settings_config&){ ... })`.
- Runtime access: `context.editor_settings->...` (an `Editor_settings_config*` on
  `App_context`, `src/editor/app_context.hpp`; assigned in `editor.cpp` after part
  construction - do NOT read it from a part constructor).

### Top-level fields and per-scene classification

Classification is from issue #239: which settings are useful to override per scene.
"Scene" = the eight settings moved to per-scene overrides; "Global" = stays editor
global.

| Field | Config struct | Purpose | #239 |
|-------|---------------|---------|------|
| `camera_controls` | Camera_controls_config | sensitivity, invert X/Y, move/turn speed, damping | Scene |
| `debug_visualizations_style` | Debug_visualizations_style | editor-global colors/widths for debug vis (~50 fields) | Global |
| `render_style_appearance` | Render_style_appearance | default (non-selected) mesh edge/point colors, widths, color sources | Global |
| `selected_render_style_appearance` | Render_style_appearance | same, for selected meshes | Global |
| `selection_outline` | Selection_outline_style | selection highlight outline color/width/frequency | Global |
| `mesh_component_style` | Mesh_component_style | vertex/edge/face component selection colors and sizes | Global |
| `gizmo_scale` | float | transform gizmo handle scale | Global |
| `clear_color` | vec4 | viewport background clear color | Scene |
| `content_edge_lines` | Content_edge_lines_config | wide-line / tent method + bias tuning | Global |
| `scene_views` | vector<Scene_view_settings> | per-viewport settings (already per-view) | (per-view) |
| `developer` | Developer_config | developer mode enable | Global |
| `grid` | Grid_config | grid snap/visible, cell size/div/count, per-level colors/widths, labels | Scene |
| `headset` | Headset_config (erhe_xr) | OpenXR / Quest config (foveation, passthrough, quad layers, perf levels) | Global |
| `hotbar` | Hotbar_config | VR hotbar UI | Global |
| `hud` | Hud_config | VR HUD panel | Global |
| `id_renderer` | Id_renderer_config | ID-buffer picking renderer | Global |
| `inventory` | Inventory_config | VR inventory grid | Global |
| `network` | Network_config | up/downstream address+port | Global |
| `physics` | Physics_config | static/dynamic enable, debug draw | Scene |
| `scene` | Scene_config | defaults for CREATING new scenes (instance count/gap, brush sets) | Global |
| `shadow_frustum_fit` | Shadow_frustum_fit_config | shadow frustum fit toggles + quantize step | Scene |
| `sky` | Sky_config | gradient/atmosphere sky, horizon/zenith colors, sun params | Scene |
| `thumbnails` | Thumbnails_config | thumbnail render cache capacity + size | Global |
| `transform_tool` | Transform_tool_config | gizmo show translate/rotate | Global |
| `viewport` | Viewport_config_data | polygon fill, edge lines, corner points, centroids, bbox/sphere toggles | Scene |
| `graphics_preset_name` | string | active graphics quality preset name | Global |
| `imgui` | Imgui_settings_config | UI font paths + sizes | Global |
| `icons` | Icon_settings_config | UI icon sizes | Global |
| `threading` | Threading_config | async worker thread count | Global |
| `post_processing` | bool | enable post-processing | Scene |
| `geometry_edit_mode` | Geometry_edit_mode (enum) | edit shared geometry vs fork-on-edit | Global |
| `transform_mode` | Mesh_transform_mode (enum) | move vs extrude component transform | Global |
| `quit_after_frames` | int | developer: exit after N frames (0=off) | Global |

Per-scene set (8): `sky`, `clear_color`, `grid`, `physics`, `shadow_frustum_fit`,
`post_processing`, `viewport`, `camera_controls`.

Note: `scene_views` already carries per-view state (per-view debug-vis toggles,
viewport render-style visibility toggles, scene+camera selection). The editor-global
appearance (colors/widths) lives in the `*_appearance` / `*_style` structs above;
new views fall back to struct defaults.

---

## 2. erhe_codegen (C++ struct generator)

`src/erhe/codegen/` - Python generator producing C++ structs with versioned JSON
(simdjson) serialize/deserialize + reflection. Docs: `doc/erhe/codegen.md`.

### Definition API (in a `<name>.py` file)

```python
from erhe_codegen import *

struct("My_config", version=2, short_desc="...", long_desc="...",
    fields=[
        field("name", String, added_in=1, default='""'),
        field("count", Int, added_in=1, default="10", ui_min="0", ui_max="100"),
        field("mode", EnumRef("My_mode"), added_in=2, default="My_mode::a"),
        field("nested", StructRef("Other_config"), added_in=1),
        field("maybe", Optional(StructRef("Other_config")), added_in=2),
        field("dead", Float, added_in=1, removed_in=2, default="1.0f"),
    ])

enum("My_mode", value("a", 0), value("b", 1), underlying_type=UInt)
```

Field knobs: `added_in` (required), `removed_in`, `default` (C++ initializer string),
`default_history=[(changed_in, prev_default), ...]` (reconstruct the default that was
current at an older doc version), `short_desc`/`long_desc`, `visible`, `developer`,
`ui_min`/`ui_max`, `hard_min`/`hard_max`, `path` (UI grouping).

Types: `Bool`, `Int`/`UInt`(+sized), `Float`, `Double`, `String`, `Vec2/3/4`,
`IVec2/3`, `Mat4`, `Vector(T)`, `Array(T,N)`, `Optional(T)`, `Map(K,V)`,
`StructRef("Name")`, `EnumRef("Name")`.

### Generated output (per struct: 3 files; per enum: 2 files)

- `foo.hpp` - plain struct, no simdjson dependency (`static constexpr uint32_t
  current_version`; all fields ever declared are present, deprecated included).
- `foo_serialization.hpp` - `serialize()`, `deserialize()`, `is_default()`,
  `get_fields()`, `get_struct_info()` declarations.
- `foo.cpp` - implementations + static reflection tables.

The hpp/serialization split matters: libraries can use the struct without pulling in
simdjson (which picks different impl namespaces under different arch flags, e.g. Jolt
compatibility).

### Optional and "is_default" (the #239 mechanism)

`Optional(T)` -> `std::optional<T>`. Serializes engaged as the value/object,
disengaged as JSON `null`; deserialize maps `null` -> `std::nullopt`. Works with
`Optional(StructRef(...))` (verified in `emit_cpp.py`
`_serialize_value_code`/`_deserialize_value_code`).

The generator emits `is_default(const T&)`; for an `Optional` field "at default"
means `!has_value()`. So "field unset = use the editor default" needs no new
primitive - a disengaged optional IS the default.

### Versioning / migration

JSON root carries `"_version": N`. `deserialize` reads it first, then version-guards
each field by `added_in`/`removed_in`. Missing fields keep member-initializer
defaults (or the `default_history` value for the doc's version). After load, if the
doc version differs, `erhe::codegen::run_migrations(out, old, new)` runs any callback
registered with `erhe::codegen::register_migration<T>(cb)`.

### Build wiring (two editor codegen units)

`src/editor/CMakeLists.txt` calls `erhe_codegen_generate()` twice:

- Scene unit (~line 473): `DEFINITIONS_DIR src/editor/scene/definitions` ->
  `.../scene/generated`. Holds `scene_file.py` and its data structs.
- Config unit (~line 596): `DEFINITIONS_DIR src/editor/config/definitions` ->
  `.../config/generated`. Holds `editor_settings_config.py` and every config struct
  (sky, grid, physics, ...). Uses
  `EXTRA_DEFINITIONS_DIRS "<dir>:<include-prefix>"` to REFERENCE (parse-only, not
  emit) structs from other libraries (graphics/renderer/scene_renderer/xr).

To reference a struct across units, add its definitions dir to the consumer unit's
`EXTRA_DEFINITIONS_DIRS` with the include prefix under which its generated headers
resolve, and list the new `.py` in `DEFINITIONS` plus its 3 generated files in the
unit's `_sources` list. No `file(GLOB)` - everything is explicit.

Command (also run at configure time by the CMake function):
`py -3 src/erhe/codegen/generate.py <defs_dir> <out_dir> [<extra_dir>:<prefix> ...]`

Gotcha: after changing a definition, the regenerated `.cpp` may not recompile in the
same build pass. Build twice, or the binary is stale.

---

## 3. Scene structure and serialization

- `erhe::scene::Scene` (`src/erhe/scene/erhe_scene/scene.hpp`) is graphics-agnostic
  data: root node, flat node vector, mesh/light layers, cameras, skins, host. No
  settings/metadata slot. Do NOT add editor concepts here.
- The editor wrapper is `Scene_root` (`src/editor/scene/scene_root.hpp`): owns the
  `Scene`, layers, Jolt physics world, raytrace scene, content library, node
  physics/joints. This is the runtime home for per-scene state.
- Scenes are registered in `App_scenes` (`src/editor/app_scenes.hpp`);
  `get_single_scene_root()` / `get_scene_roots()`.
- Active-scene access from a frame consumer:
  `Scene_view::get_scene_root()->get_scene()` (`src/editor/scene/scene_view.hpp`);
  `Viewport_scene_view` and `Headset_view` are the concrete Scene_views.

### Save / load

- Scenes persist as a single erhe-authored glTF file (`<name>.glb`); the full
  process reference is `doc/editor/scene_serialization.md`.
- The scene codegen unit is `src/editor/scene/definitions/`.
- `Scene_settings` is stored on `Scene_root` and serialized inside the
  `ERHE_scene` glTF extension on the scene object, together with
  `ambient_light` and `enable_physics`
  (`doc/gltf_extensions/ERHE_scene.md`). That extension is where a new
  scene-level field goes.

---

## 4. Per-scene setting overrides

`Scene_settings` holds one `Optional(StructRef(...))` or `Optional(scalar)` per
overridable setting: disengaged means "use the editor global". It is stored on
`Scene_root` and the override UI is in the Properties window for the selected
Scene, which is also where a scene's ambient light is edited.

`src/editor/scene/scene_settings_resolve.hpp` is the single place that resolves
an effective value - the scene's override when it is engaged, the editor
setting otherwise: `get_effective_sky`, `get_effective_grid`,
`get_effective_physics`, `get_effective_shadow_frustum_fit`,
`get_effective_camera_controls`, `get_effective_clear_color`,
`get_effective_post_processing`. A consumer that reads its setting once at init
time has to be refactored to ask per scene before the corresponding override
can take effect.

---

## Future work

- [plans/editor.md](../plans/editor.md) - the per-scene overrides whose consumers
  still read their setting once at init time.
