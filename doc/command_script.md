# Editor command scripts

The editor reads `config/editor/commands.json` at startup and invokes a list
of named `erhe::commands::Command` instances once, in order, before the main
loop begins. The same `Command` objects also back the corresponding UI
buttons and menu items, so the script and the UI go through the same code
path.

## Config file

`config/editor/commands.json` is loaded via `erhe::codegen::load_config<Commands_config>`.
The schema lives in `src/editor/config/definitions/commands_config.py` and
the generated struct is `Commands_config` (mirrored fields plus a
`std::vector<std::string> commands`).

Default file:

```json
{
    "instance_count": 1,
    "instance_gap": 0.5,
    "object_scale": 1.0,
    "detail": 4,
    "commands": [
        "scene.add_lights",
        "scene.add_room"
    ]
}
```

| Key             | Type     | Purpose |
|-----------------|----------|---------|
| `instance_count`| int      | `Make_mesh_config.instance_count` for the `scene.add_*` mesh commands. |
| `instance_gap`  | float    | `Make_mesh_config.instance_gap`. |
| `object_scale`  | float    | `Make_mesh_config.object_scale`. |
| `detail`        | int      | `Make_mesh_config.detail`. |
| `commands`      | string[] | Sequence of registered command names to invoke at startup. |

Notes:

- A missing `commands.json` or empty `commands` array is a valid no-op.
  The four scalar fields are pushed into all five `scene.add_*` mesh
  commands before the script runs, but they only matter if at least one
  of those commands is invoked.
- The `Make_mesh_config` block in `commands.json` is intentionally
  separate from the Operations window's UI-edited copy. The script gets
  reproducible values that do not depend on UI state.
- `Make_mesh_config.material` is not in the JSON. The mesh commands run
  with `material = nullptr`, which is safe across all five paths
  (`make_mesh_nodes` ignores `config.material` entirely;
  `add_torus_chain` falls back to the next library material when null).

## Available commands

All names live in the `scene.*` namespace and are zero-argument
(`Command::try_call()`).

| Name                          | Effect                                                                  | Undoable |
|-------------------------------|-------------------------------------------------------------------------|----------|
| `scene.add_room`              | Adds a single floor instance (locked from viewport edit).               | yes      |
| `scene.add_lights`            | Adds the directional/spot lights configured in `Scene_config` AND sets the light layer's ambient color. | yes (single undo restores ambient + removes all lights) |
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
   per-editor configs in its member-init list
   (`editor.cpp:578-585` area).
2. The constructor builds all subsystems (including `Scene_builder`,
   `Scene_commands`, `Operation_stack`) via the parallel taskflow, then
   `fill_app_context()` wires the raw pointers into `App_context`.
3. `Editor::run()` calls `run_startup_script()` immediately after
   `m_run_started = true;` and before the main loop. By this point
   every subsystem the script can touch is live.
4. `run_startup_script()` builds a `Make_mesh_config` from the loaded
   scalar fields, pushes it into all five `scene.add_*` mesh commands,
   then iterates `m_commands_config.commands`, calling
   `Commands::find_command(name)->try_call()` on each. Unknown names
   produce a `log_startup` warning and are skipped.
5. Each `try_call()` queues a `Compound_operation` on
   `m_app_context.operation_stack`. The actual scene mutations happen
   on the first tick when `Operation_stack::update()` drains the queue.

The single-tick deferral is why the Quest / OpenXR build does not flash
an empty scene: by the time the first frame is presented, `update()`
has already executed the queued floor + lights + meshes.

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
5. The new name is automatically reachable from
   `commands.json` -- no changes to the codegen schema or the loader
   are required.

## Limitations and future work

- **Camera setup is not yet command-driven.** `Scene_builder::setup_cameras`
  still runs from the `Scene_builder` constructor and inserts "Camera A"
  via direct `node->set_parent(...)`. Converting it to `scene.add_cameras`
  needs a clean split between the camera node (which fits
  `Item_insert_remove_operation`) and the `Viewport_scene_view` /
  `Viewport_window` plumbing it builds (which does not). The existing
  `scene.create_new_camera` shows the camera-only undoable shape -- the
  plumbing has to remain a one-way side effect or move to a separate
  non-undoable post-init phase.
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
