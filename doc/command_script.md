# Editor command scripts

The editor reads `config/editor/commands.json` at startup and invokes a list
of named `erhe::commands::Command` instances once, in order, before the main
loop begins. The same `Command` objects also back the corresponding UI
buttons and menu items, so the script and the UI go through the same code
path.

## Config file

`config/editor/commands.json` is loaded by `Editor::run_startup_script()`
directly via simdjson. The top-level shape is a single `commands` array
whose elements are either bare strings (no args) or
`{"name": "...", "args": {...}}` objects. Per-command args structs are
codegen-generated; the polymorphic top-level array is hand-rolled
because codegen does not express a string-or-object union.

Default file:

```json
{
    "commands": [
        {
            "name": "scene.add_cameras",
            "args": {
                "camera_exposure": 1,
                "shadow_range": 30,
                "camera_distance": 4,
                "camera_elevation": 1.3
            }
        },
        {
            "name": "scene.add_lights",
            "args": {
                "_version": 2,
                "directional_light_intensity": 4,
                "directional_light_radius": 4.5,
                "directional_light_height": 10,
                "directional_light_shadow_count": 4,
                "directional_light_no_shadow_count": 4,
                "spot_light_intensity": 500,
                "spot_light_radius": 10,
                "spot_light_height": 5,
                "spot_light_shadow_count": 0,
                "spot_light_no_shadow_count": 0,
                "point_light_intensity": 150,
                "point_light_radius": 15,
                "point_light_height": 8,
                "point_light_shadow_count": 0,
                "point_light_no_shadow_count": 0
            }
        },
        {
            "name": "scene.add_room",
            "args": {
                "floor": true,
                "floor_size": 32,
                "floor_height": 1
            }
        },
        {
            "name": "scene.add_platonic_solids",
            "args": {
                "instance_count": 1,
                "instance_gap": 0.5,
                "object_scale": 0.25,
                "detail": 4,
                "mass_scale": 8
            }
        }
    ]
}
```

`scene.add_*` mesh commands all take the same `Make_mesh_args` shape today
(instance_count, instance_gap, object_scale, detail, mass_scale). `detail`
and `mass_scale` only have effect on the FIRST mesh-command invocation --
that call drives the lazy `Scene_builder::ensure_brushes(...)` which builds
the brush set used by every subsequent mesh command. Re-running a mesh
command later with different `detail` / `mass_scale` does NOT rebuild
brushes; the cached set wins. (`instance_count`, `instance_gap`,
`object_scale` are honored per-invocation since they apply at instance
placement, not at brush construction.)

Notes:

- A missing `commands.json`, missing `commands` array, or empty array is
  a valid no-op.
- Each entry's args (if any) belong to that one invocation. Repeating
  the same command twice with different args is fine; each invocation
  uses its own args block.
- An entry that is a bare string is equivalent to the same name with no
  `args` block: the command is invoked with default-constructed args
  (e.g. `Make_mesh_args{}` for the mesh commands).
- An `args` block on a command that does not take args produces a
  `log_startup` warning and is otherwise ignored.
- The `Make_mesh_config::material` field is not in `Make_mesh_args`.
  Script-driven invocations always run with `material = nullptr`, which
  is safe across the five mesh commands (`make_mesh_nodes` ignores
  `config.material`; `add_torus_chain` falls back to the next library
  material when null). The Operations window button path goes through
  `set_make_mesh_config(...)` instead, populating material from the
  current selection.

## Available commands

All names live in the `scene.*` namespace and are zero-argument
(`Command::try_call()`).

| Name                          | Effect                                                                  | Undoable |
|-------------------------------|-------------------------------------------------------------------------|----------|
| `scene.add_cameras`           | Builds default camera(s) from its `Add_cameras_args` (camera_distance, camera_elevation, camera_exposure, shadow_range) and -- on desktop builds with `imgui_window_scene_view` enabled -- the default `Viewport_scene_view` + `Viewport_window` bound to "Camera A". | partial (camera node insertion is undoable; the viewport / rendergraph plumbing is not) |
| `scene.add_room`              | Builds a floor brush from its `Add_room_args` and queues an undoable insert of one floor instance (locked from viewport edit). `args.floor: false` makes it a no-op. | yes      |
| `scene.add_lights`            | Adds the directional/spot/point lights described by its `Add_lights_args` (each light type split into shadow-casting and non-shadow-casting counts) and sets the light layer's ambient color. | yes (single undo restores ambient + removes all lights) |
| `scene.add_platonic_solids`   | Adds one instance per platonic-solid brush, packed in a 2D layout.      | yes      |
| `scene.add_johnson_solids`    | Adds one instance per Johnson-solid brush.                              | yes      |
| `scene.add_curved_shapes`     | Adds sphere / two cylinder variants / cone / torus instances.           | yes      |
| `scene.add_chain`             | Adds a chain of interlocking toruses (alternating-axis pairs).          | yes      |
| `scene.add_toruses`           | Adds a row of separate toruses.                                         | yes      |
| `scene.create_new_camera`     | Adds a single new camera node. (Pre-existing.)                          | yes      |
| `scene.create_new_empty_node` | Adds a single empty node. (Pre-existing.)                               | yes      |
| `scene.create_new_light`      | Adds a single generic light. (Pre-existing.)                            | yes      |
| `scene.create_new_rendertarget`| Adds a rendertarget mesh + viewport bound to the selected camera.      | partial (the queued node insert is undoable; the viewport / imgui-host wiring is not) |

The `scene.add_*` mesh commands also share their `Make_mesh_config` with
the Operations window buttons via `Scene_commands::get_add_*_command()`:
when a button fires, it pushes the window's UI config into the command
before calling `try_call()`. Both code paths produce the same undoable
`Compound_operation`.

## Execution model

1. `Editor()` constructor loads `Commands_config` alongside the other
   per-editor configs in its member-init list.
2. The constructor builds all subsystems (including `Scene_builder`,
   `Scene_commands`, `Operation_stack`) via the parallel taskflow, then
   `fill_app_context()` wires the raw pointers into `App_context`.
3. The constructor body calls `run_startup_script()` near the end --
   after every subsystem is wired into `App_context`, but BEFORE the
   init-time command buffer is closed (`m_app_context.current_command_buffer->end()`).
   This ordering matters: several scripted commands record GPU work
   during their `try_call()` (`scene.add_cameras` builds the default
   `Viewport_scene_view` and its `Shadow_render_node`; `scene.add_*`
   meshes invoke `Brush::make_instance`), and they need a valid
   `current_command_buffer` to do so.
4. `run_startup_script()` builds a `Make_mesh_config` from the loaded
   scalar fields, pushes it into all five `scene.add_*` mesh commands,
   then iterates `m_commands_config.commands`, calling
   `Commands::find_command(name)->try_call()` on each. Unknown names
   produce a `log_startup` warning and are skipped.
5. Each `try_call()` queues a `Compound_operation` on
   `m_app_context.operation_stack`. The actual scene-graph mutations
   (`Item_insert_remove_operation::execute` and
   `Ambient_light_operation::execute`) run later when
   `Operation_stack::update()` drains the queue on the first tick. By
   then the per-frame command buffer is open, but the operation
   `execute` paths only touch CPU-side scene state, so they do not need
   one.

The init-cb / first-tick handoff is why the editor does not flash an
empty scene: GPU resources required by the viewport (shadow textures,
bindless uploads done inside `make_instance`) are recorded on the init
cb, and the deferred `set_parent` calls land before any rendering
begins.

## Undo semantics

Every `scene.add_*` command queues exactly one `Compound_operation`. A
single Undo therefore reverses one whole batch (the entire torus chain,
all five Platonic solids, every default light + ambient, etc.). Mesh
commands wrap one `Item_insert_remove_operation` per node;
`scene.add_lights` additionally prepends an `Ambient_light_operation`
(`src/editor/operations/ambient_light_operation.{hpp,cpp}`) so the
ambient color change rides on the same compound and is restored on
undo, re-applied on redo.

`Compound_operation::undo` walks the contained operations in reverse,
which is why ambient is queued first (executed first, undone last).

## Adding a new command

Mirror the existing `scene.add_room` end-to-end:

1. Add a new `Command` subclass to `src/editor/scene/scene_commands.hpp`
   (or wherever the natural owner lives). Constructor takes
   `(erhe::commands::Commands&, App_context&)`, passes the stable
   string name to the `Command` base, stores `App_context&`. Add a
   getter on `Scene_commands` if other subsystems need to set state on
   it (mesh commands do this for `Make_mesh_config`).
2. Implement `try_call()` to either mutate directly (only valid for
   non-undoable side effects) or build a `Compound_operation` of one or
   more `Operation` subclasses and queue it via
   `m_context.operation_stack->queue(...)`. Any node insertion belongs
   in `Item_insert_remove_operation` with `Mode::insert`. State-bag
   mutations get a dedicated `Operation` subclass (see
   `Ambient_light_operation` for a 30-line template).
3. Construct, register, and expose the command in `Scene_commands`:
   member-init, `commands.register_command(&m_my_command);`, and a
   `get_my_command()` accessor if needed.
4. If a UI button should also drive it, replace the button body with
   `m_context.scene_commands->get_my_command().try_call();` (push
   per-button state in via a setter first if applicable).
5. The new name is automatically reachable as a bare string entry in
   `commands.json` -- no further wiring needed for an args-less command.

If the new command takes per-invocation args:

6. Add a codegen def for the args struct under
   `src/editor/config/definitions/<my>_args.py` (or extend an existing
   one if the args are identical to an established struct -- the five
   `scene.add_*` mesh commands all share `Make_mesh_args` for this
   reason). List the new `.py` in the `DEFINITIONS` block of
   `src/editor/CMakeLists.txt` and add the three generated source
   filenames to `_config_sources`.
7. Add `void apply_args(const My_args&);` on the command class that
   copies the deserialized args into whatever runtime state
   `try_call()` consumes.
8. Add a dispatch arm to `Editor::run_startup_script()`: deserialize
   the `args` sub-object into `My_args` via the codegen-generated
   `deserialize(...)` and call `command.apply_args(...)` before
   `command->try_call()`. Keep the bare-string path working by
   default-constructing the args struct when no `args` block is given.

## Limitations and future work

- **`scene.add_cameras` undoes the cameras but not the viewport.** The
  desktop default `Viewport_scene_view` + `Viewport_window` are
  rendergraph / imgui plumbing tied to "Camera A"; they are not scene
  nodes and don't fit `Item_insert_remove_operation`. Today they are
  created as a non-undoable side effect of `scene.add_cameras` after
  the camera-node compound op is queued. Undoing the command removes
  the camera nodes; the orphaned viewport stays. Re-running the command
  would create a second viewport on top of the first. If the user
  workflow ever calls for re-running it, the viewport plumbing should
  move to its own one-shot setup hook (or a dedicated non-undoable
  command) so it is not duplicated.
- **`Scene_builder::add_cubes` is not undoable** but is also dead-coded
  (the only call site is `#if 0` in `operations_window.cpp`). Convert
  it together with the button revival, or delete it.
- **`Scene_builder::animate_lights` mutates transforms every frame** and
  is intentionally not undoable -- per-frame transform animation does
  not belong on the undo stack.
- **No script arguments.** Each command is zero-arg; `add_torus_chain`'s
  bool is exposed as the two distinct names `scene.add_chain` /
  `scene.add_toruses`. If a future command genuinely needs arguments,
  the script format would have to grow beyond a flat list of strings
  (e.g. objects with `name` + `args`) and a small dispatch layer above
  `Command::try_call()`.
- **No manual re-run trigger.** The script fires exactly once at
  startup. If iterating on a script becomes a workflow, a Developer-menu
  "Run startup script" button on `Commands_window` would let users
  replay it without restarting the editor.
- **No script files separate from the config.** Multiple named scripts
  (e.g. one per saved scene preset) would require either multiple
  `commands_*.json` files chosen at runtime or a top-level map of named
  command lists in a single JSON. Not implemented.
- **Scalar fields apply globally to mesh commands.** A future need for
  per-command `Make_mesh_config` (e.g. small Platonic solids next to a
  large torus) would require either pre-set commands with their own
  config or args on the script lines (see "No script arguments" above).
- **`commands.json` defaults are global, not per-scene.** Loading a
  saved scene does not re-run the script; the script is purely an
  initial-scene authoring tool.
