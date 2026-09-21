# erhe Editor MCP Server

Stability: mostly stable

The editor embeds an MCP (Model Context Protocol) server that exposes editor commands and scene/content-library queries over HTTP using JSON-RPC 2.0. The server starts automatically with the editor on `127.0.0.1:3743` ("erhe" on a phone keypad). The `ERHE_MCP_PORT` environment variable overrides the preferred port, and `ERHE_MCP_TOKEN_FILE` names the bearer-token file explicitly (default `~/.agents/erhe_mcp_token`; the mode 0600 requirement applies to both). If the preferred port is already in use the server falls back to the next free port, scanning 20 successors (`[3743, 3763)` by default); the port it actually bound is logged as `MCP server: listening on 127.0.0.1:<port>`. The client scripts (`scripts/mcp_call.py`, `scripts/erhe_mcp.py`) also honor `ERHE_MCP_PORT` for their default port.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC 2.0 endpoint for MCP protocol |
| GET | `/health` | Readiness check: `200 {"status":"ok"}` once the main loop serves requests, `503 {"status":"starting"}` while the editor is still starting (the HTTP thread is up seconds before the first frame; a `tools/call` sent then expires) |

## Registering as an HTTP MCP server

The server is a spec-conformant MCP HTTP endpoint (request/response only, no
SSE stream), so MCP clients can register it directly and get native
`mcp__erhe__*` tools instead of driving raw HTTP. Verified end-to-end with
Claude Code 2026-08-22 (handshake, `tools/list`, native tool calls):

```bash
claude mcp add --transport http erhe http://127.0.0.1:3743/mcp
claude mcp list        # erhe: ... - Connected  (requires the editor to be running)
```

or equivalently in a project `.mcp.json`:

```json
{
  "mcpServers": {
    "erhe": {
      "type": "http",
      "url": "http://127.0.0.1:3743/mcp"
    }
  }
}
```

Caveats:

- **The server only exists while the editor runs.** A client session started
  before the editor shows the server as failed/disconnected; reconnect (in
  Claude Code: `/mcp`) or start a new session after launching the editor.
  `scripts/mcp_call.py` remains the right tool for scripted flows that
  launch the editor themselves.
- **Register a fixed port.** The registration pins one URL, but the editor's
  fallback scan may move the server off 3743 when the port is taken (e.g. a
  second editor). Set `ERHE_MCP_PORT` to give an instance a deterministic
  port, and verify identity with `get_server_info` (it reports pid + build
  timestamp).
- **Bearer auth**: if `~/.agents/erhe_mcp_token` exists the server requires
  the token; add it to the registration:
  `claude mcp add --transport http erhe http://127.0.0.1:3743/mcp --header "Authorization: Bearer <token>"`.
- **The tool list is a startup snapshot.** The server does not send
  `tools/list_changed`, so tools added at runtime (registered editor
  commands) appear only after the client re-lists.

## MCP Methods

All requests are JSON-RPC 2.0 POST to `/mcp`.

### initialize

Handshake - returns server info and capabilities.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"initialize"}'
```

### tools/list

List all available tools (query tools + editor commands).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"2","method":"tools/list"}'
```

### tools/call

Invoke a tool by name. All tools are queued for execution on the main editor thread (5-second timeout).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"3","method":"tools/call","params":{"name":"TOOL_NAME","arguments":{}}}'
```

## Query Tools

These tools query editor state and return structured JSON data.

### list_scenes

List all scenes with summary counts.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"list_scenes","arguments":{}}}'
```

Returns: `{scenes: [{name, node_count, camera_count, light_count, material_count}]}`

### get_scene_nodes

List every prim of a scene's tree with its class, place and - for the prims
that have one - transform and attachment info.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_nodes","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{nodes: [{name, id, type, parent, parent_id, locked, active, import_root, tags}]}`,
each transformable prim additionally carrying `position`, `rotation_xyzw`,
`scale` and `attachment_types`. `type` is the prim's class name (`Xform`,
`Mesh`, `Camera`, `Light`, `Scope`, ...) and `attachment_types` names the
applied-API-schema attachments alone; a prim outside `Xformable` - a `Scope` -
has no transform and no attachments, and the prims below it are listed with it
as their `parent`.

### get_node_details

Get detailed info for a specific prim including world position, local transform, the prim's own class section, attachments, children, and selection state. A `Mesh` prim carries a `mesh` section (materials, primitive and vertex counts, world AABB, layer diagnostics), a `Camera` prim a `camera` section (`exposure`, `shadow_range`) and a `Light` prim a `light` section (`light_type`, `color`, `intensity`, `range`); the key is `null` on a prim of another class. A node placed by a brush carries a `brush_placement` section (`brush_name`, `brush_id`, `facet`, `corner`), null on every other node. `attachments` lists the applied-API-schema attachments alone (`Node_physics`, `Node_joint`, `Prefab_instance`, `Frame_controller`, `Grid`), because a `Mesh`, `Camera` or `Light` is a child prim and answers as its own node. A `Prefab_instance` attachment carries `prefab_source_path`, `prefab_name` and `prefab_prim_path` (the prim a USD `references` arc named, empty for a glTF prefab); a prim that authors several arcs carries one attachment per arc, in the arcs' order. `parent` is the prim's parent in the tree and `transform_parent` the nearest transformable ancestor its world transform composes with (they differ when a `Scope` sits between them). A prim outside `Xformable` answers with its `type`, place and children alone. Every entry carries `active`: the effective `Item_flags::active` bit, false for an inactive prim and for everything below one (doc/erhe/usd_compatibility_design.md X2).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_node_details","arguments":{"scene_name":"Default Scene","node_name":"Cube"}}}'
```

### get_scene_cameras

List all cameras in a scene.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_cameras","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{cameras: [{name, node, exposure, shadow_range}]}`

### get_scene_lights

List all lights in a scene.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_lights","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{lights: [{name, node, type, color, intensity, range}]}`

### get_scene_materials

List all materials in a scene's content library.

A scene's content-library resources are prims of the scene's own tree
(`doc/erhe/usd_compatibility_design.md` U4), under the `Scope`s named for their kind
(`Materials`, `Textures`, `Brushes`, `Styles`, `Physics Materials`, ...), so
`get_scene_nodes` reports them with their class as `type` and the property
tools address them by `item_name`, `item_id` or path like any prim.
Any prim may be the child of any other prim: `create_scope` creates the
`Scope`s (folders) along a slash-separated `path` below any prim, and
`reparent_item` moves any prim - a node, a resource or a `Scope` - under any
prim, naming each by id, by a name that names exactly one prim, or by path.
Every `create_*` tool that makes a resource - `create_material`,
`create_style`, `create_physics_material`, `create_collision_filter`,
`create_physics_joint_settings`, `create_graph_texture`, `create_graph_mesh` -
takes an optional `parent_id` / `parent_name` (any prim; without one the
resource goes into its kind scope) and inserts through an
`Item_insert_remove_operation`, so it is undoable; `copy_library_item` is not.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_materials","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{materials: [{name, base_color, metallic, roughness, emissive}]}`

### get_material_details

Get full material properties including texture presence flags.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_material_details","arguments":{"scene_name":"Default Scene","material_name":"Gold"}}}'
```

Returns: `{name, base_color, opacity, roughness, metallic, reflectance, emissive, unlit, has_base_color_texture, ...}`

### get_scene_brushes

List all brushes in a scene's content library.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_brushes","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{brushes: [{name, id, folder_path, geometry_state, vertex_count, facet_count, material, density, normal_style}]}` -
`material` is the name of the material a placed instance gets, or `null`;
`normal_style` is the token a USD `erhe:Brush:normal_style` attribute and the
glTF `ERHE_brushes` field are spelled with.
`folder_path` is the scope path below the `Brushes` scope, empty for a brush
directly under it.
`geometry_state` is `unprepared`, `queued`, `preparing`, `ready` or `failed`
(`doc/editor/brushes.md` G1). Brush geometry is prepared on demand, and this
query is a tier 2 consumer (G3): it asks the preparation queue for every
listed brush and returns at once, so `vertex_count` and `facet_count` carry
counts only for a brush that is already `ready` and are `null` otherwise.

### get_brush_geometry_states

Report the geometry state of a scene's palette brushes without preparing any
of them: per-state counts plus the state of each brush named in the optional
`brush_names` array (every brush of the palette when it is absent). This is
how a caller waits for a `request_brush_geometry` batch to finish.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_brush_geometry_states","arguments":{"scene_name":"Default Scene"}}}'
```

### request_brush_geometry

Ask the preparation queue to build the geometry of a scene's palette brushes
on worker threads and return at once (tier 2). Takes the same `scene_name` and
optional `brush_names`, and returns the same counts as
`get_brush_geometry_states`, taken right after the requests were made; poll
`get_brush_geometry_states` until nothing is `queued` or `preparing`.

### get_scene_variants

List the variant sets a scene carries (doc/erhe/usd_compatibility_design.md X4): a
USD file's material-binding `variantSet`s become one entry each, and a glTF
file's `KHR_materials_variants` list one entry named `materials`, carried by
the prim the file's content sits under (the scene's root prim, whose path is
empty, for a scene opened from a file; the import root for an imported
asset).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_variants","arguments":{"scene_name":"variants"}}}'
```

Returns: `{scene_name, variant_sets: [{prim_path, set_name, selected,
variants: [{name, bindings: [{relative_path, material}]}],
unsupported_opinion_count}]}` - `prim_path` is the path of the prim carrying
the set, `relative_path` the path of the bound prim below it (empty for that
prim itself, and `<mesh path>#<primitive index>` for a primitive a file names
only by position, which is every glTF primitive), and `unsupported_opinion_count` how many opinions beyond
material bindings the file's variants authored, which this slice neither
applies nor writes back.

### pick_at

Headless pick probe: arm the pointer at viewport pixel coordinates and run
the same hover update a real pointer runs. Reports every hover slot
(content / tool / brush / rendertarget / grid / bone) plus `nearest` - the
hit resolved by the same slot-ownership rule a viewport click uses (in bone
selection mode the bone slot replaces content, and a bone hit reports the
`joint` a click would select).

The id-render readback is asynchronous: the first call at a position returns
raytrace hits only (which never include skinned meshes). The armed position
keeps the id pass rendering there for a few frames, so call again shortly
after for the merged result that includes id-picked (e.g. skinned) meshes.

```bash
py -3 scripts/mcp_call.py pick_at b64:eyJ4IjogNjU2LCAieSI6IDI4Mn0=   # {"x": 656, "y": 282}
```

Arguments: `x`, `y` (viewport pixels, origin bottom-left), optional
`viewport` (window title from `get_viewports`; default: first viewport).

Returns: `{x, y, slots: [{slot, valid, mesh?, node?, joint?, grid?, position?, normal?, facet?}], nearest}`

### get_selection

Get currently selected items, and the active item: the one reference item of
the selection (doc/editor/active_item.md). The active item can be an item that
is no longer selected, which is why it is reported with its own `selected`
flag.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_selection","arguments":{}}}'
```

Returns: `{items: [{name, type, id, scene_name?}], active_item?: {name, type, id, scene_name?, selected}, active_scene?, ...}`

### select_items

Select items by unique ID. Searches scene nodes, cameras, lights, materials, and brushes.
The last listed item becomes the active item; the optional `active` argument
(an id or a name / path) names another one of the listed items instead.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"select_items","arguments":{"scene_name":"Default Scene","ids":[716,720]}}}'
```

Returns: `{selected_count, items: [{name, type, id}], active_item?: {name, type, id}}`

Pass an empty `ids` array to clear selection. Clearing the selection leaves the
active item as it is. All query responses include `id` fields for use with this tool.

### set_active_item

Make one item the active item: the single reference item commands act on. The
item does not have to be selected, and the selection is left unchanged. Takes
`scene_name` plus `id` or `path`.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"set_active_item","arguments":{"scene_name":"Default Scene","path":"Cube"}}}'
```

Returns: `{active_item: {name, type, id, selected}}`

### attach_selection_to_active

The Operations window "Attach" button (Blender Ctrl-P): parent every node of
the command target selection other than the active node under the active node,
as one undoable compound operation. Takes no arguments; name the parent with
`set_active_item` first (or make it the last item of `select_items`). Nodes
that are ancestors of the active node are skipped.

```bash
curl -X POST http://127.0.0.1:3743/mcp   -H "Content-Type: application/json"   -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"attach_selection_to_active","arguments":{}}}'
```

Returns: `{queued, active_item: {name, type, id}}`

## Action Tools

### create_node

Create an empty prim (undoable, inserted on the next editor frame).
`prim_type` selects the class: `Xform` (default; a transform with children) or
`Scope` (children only, no transform - what resources are gathered under). A
`Scope` accepts no `position`.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_node","arguments":{"scene_name":"Default Scene","name":"Materials","prim_type":"Scope"}}}'
```

Returns: `{node_name, node_id, prim_type, parent, queued}` (plus `position` for
an `Xform`).

### place_brush

Place a brush instance in a scene at a given world position.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"place_brush","arguments":{"scene_name":"Default Scene","brush_id":354,"position":[0,0.5,0],"material_name":"Gold","scale":1.0}}}'
```

Parameters:
- `scene_name` (required) - target scene
- `brush_id` (required) - brush ID from `get_scene_brushes`
- `position` (required) - `[x, y, z]` world position
- `material_name` (optional) - material name, defaults to first available
- `scale` (optional) - scale factor, default 1.0
- `motion_mode` (optional) - `"static"` or `"dynamic"` (default)

Returns: `{node_name, node_id, brush, material, position, scale}`

### create_skin

Build ONE rigidly skinned mesh out of mesh prims that are already in the scene
(undoable; the compound operation executes on the next editor frame). Each
entry of `parts` names a mesh prim and the joint node that drives every vertex
of that mesh: the part geometries are merged into a single `Geometry` with
their world transforms baked in, and each part's vertices get
`joint_indices_0 = (joint, 0, 0, 0)` and `joint_weights_0 = (1, 0, 0, 0)`. The
skin's joints are the distinct joint nodes in order of first appearance in
`parts`, its inverse bind matrices are the inverses of the joint world
transforms at the time of the call, and it names no skeleton (the transform
root is computed from the joints). The new mesh is built with the skinned
vertex format, so the GPU buffer carries the joint streams; the part mesh
prims are removed from the scene.

Rigid weights only - the weight paint tool authors smooth weights. Refuses a
part that carries no mesh, a part that is already skinned, an empty `parts`
array, an unknown id and a joint in another scene.

```bash
py -3 scripts/mcp_call.py create_skin b64:<base64 of {"scene_name":"Scene 1","name":"arm","parent_node_id":5535,"parts":[{"node_id":5540,"joint_node_id":5536},{"node_id":5545,"joint_node_id":5537}]}>
```

Parameters:
- `scene_name` (required) - target scene
- `parts` (required) - `[{node_id, joint_node_id}, ...]`, at least one
- `name` (optional) - name of the new mesh prim; the skin is `"<name> skin"`
- `parent_node_id` / `parent_node_name` (optional) - parent prim, default scene root
- `material` (optional) - material name, default the first part's material

Returns: `{node_name, node_id, skin_name, skin_id, joint_count, vertex_count,
material, joints: [{joint_index, node_name, node_id, vertex_count}], queued}`.

`get_node_details` reports `skinned`, and for a skinned mesh also `skin_name`,
`skin_id`, `joint_count` and the `joints` list.

### select_variant

Select one variant of one variant set: the chosen variant's material bindings
are assigned and the selection is recorded in the scene settings
(`Scene_settings.variant_selections`, saved with the scene), as ONE undoable
operation. Use `get_scene_variants` for the paths and names; an empty
`prim_path` names the scene's root prim.

```bash
py -3 scripts/mcp_call.py select_variant b64:<base64 of {"scene_name":"variants","prim_path":"World/Holder","set_name":"look","variant_name":"red"}>
```

Returns: `{queued, scene_name, prim_path, set_name, variant_name}`.

### toggle_physics

Toggle dynamic physics simulation on/off.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"toggle_physics","arguments":{}}}'
```

Returns: `{dynamic_physics_enabled: true/false}`

### instantiate_prefab / reload_prefab / get_prefabs / set_prefab_template_property

`instantiate_prefab` places a prefab (a glTF file, or a USD file at a prim) into a scene as a carrier prim with a `Prefab_instance` attachment and the template's content cloned below it; `reload_prefab` re-reads a prefab's file and refreshes every instance, keeping their overrides; `get_prefabs` lists the loaded templates. `set_prefab_template_property` sets (or, with a null value, clears) a local value on an item INSIDE a template - `source_path`, optional `prim_path`, `item_path` (the M1 path below the template root), `property`, `value` - which no scene lookup reaches otherwise; every instance reads the change live through its reference layer. It is not undoable, like `reload_prefab`. An item inside an instance is edited with `set_item_property` (a local value there is an override; a null value clears it and exposes the template's value) and reports `"source": "reference"` in `get_item_properties` for what the template supplies.

### get_item_properties

Lists the registered properties of one item. Beside the erhe value source
(`source`, `local`, `default`, `inherits`, `style`) each property of the item
carries `origin`, the same value's provenance in the terms of the file the
scene was opened from (`doc/erhe/usd_compatibility_design.md` X5, whose table in
`doc/erhe/usd_compatibility.md` says what each source reports):

- `layer` - the file the value is authored in, or `session` for a scene that
  has no file yet;
- `prim_path` - the prim path in that layer that authors it (an override
  inside a reference instance reports the `over`'s collapsed path);
- `arc` - `none`, `root layer`, `reference`, `payload` or `inherits`;
- `arc_target` - the arc's `<file></prim>` target, or the class prim of a
  style;
- `authored_as` - the attribute a save spells the value as
  (`erhe:Material:roughness`, `surface.inputs:diffuseColor`, `xformOp:*`, or
  `properties["Owner.name"]` for a glTF-backed scene).

The properties of a sub-object (a mesh primitive) carry no `origin`: no file
spells a sub-object as a prim of its own.

### lock_items / unlock_items

Lock or unlock items by ID. Locked items (`lock_edit` flag) cannot be deleted or have properties edited.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"lock_items","arguments":{"scene_name":"Default Scene","ids":[506]}}}'
```

### add_tags / remove_tags

Add or remove string tags on items by ID.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"add_tags","arguments":{"scene_name":"Default Scene","ids":[506],"tags":["important"]}}}'
```

### get_undo_redo_stack

Get the undo/redo operation history.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_undo_redo_stack","arguments":{}}}'
```

Returns: `{undo: [{description, error?}], redo: [{description, error?}], can_undo, can_redo}`

### get_async_status

Get pending/running async operation counts. The scene is settled only when
`pending`, `running`, `queued_operations`, `pending_scene_commits` and
`asset_loads` are all 0.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_async_status","arguments":{}}}'
```

Returns: `{pending, running, queued_operations, pending_scene_commits, asset_loads, lightmap_prepare?}`

## Physics Tools

Create and edit KHR_physics_rigid_bodies features: rigid body / joint node attachments and the shared content-library items (physics materials, collision filters, joint settings). Creation tools queue undoable operations ("queued": true in the response - the object exists on the next editor frame). Edit tools apply immediately. Nodes are addressed by `node_id` (preferred) or `node_name`.

### get_physics_items

List the shared physics content-library items with full properties.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_physics_items","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{physics_materials: [...], collision_filters: [...], physics_joint_settings: [...]}`

### create_physics_body / edit_physics_body

Attach a rigid body (Node_physics) to a node / edit it. One rigid body per node. `get_node_details` reports the body state (`motion_mode`, `collision_shape`, `mass`, `is_trigger`, `physics_material`, `collision_filter`, ...).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_body","arguments":{"scene_name":"Default Scene","node_name":"Cube","shape":"box","half_extents":[0.5,0.5,0.5],"motion_mode":"dynamic","mass":2.0}}}'
```

Parameters (all optional except `scene_name` + node reference):
- `shape` - `auto` (default: convex hull from the node's mesh, unit box without one), `box`, `sphere`, `capsule`, `tapered_capsule`, `cylinder`, `tapered_cylinder`, `convex_hull`, `mesh` (static/kinematic only); with shape params `half_extents`, `radius`, `bottom_radius`, `top_radius`, `length`, `axis`
- `motion_mode` - `static`, `kinematic`, `kinematic_non_physical`, `dynamic` (default)
- `mass`, `friction`, `restitution`, `linear_damping`, `angular_damping`, `gravity_factor`
- `is_trigger` - create as sensor/trigger volume
- `linear_velocity`, `angular_velocity` - initial velocities `[x, y, z]` (applied at body creation)
- `center_of_mass` - `[x, y, z]` offset
- `material_name`, `filter_name` - shared item names from the content library (empty string clears in edit)

`edit_physics_body` takes the same fields; only fields supplied are changed. Shape fields replace the collision shape and recreate the body. `mass` / `friction` / `restitution` / damping edit the live body.

### create_physics_joint / edit_physics_joint

Attach a joint (Node_joint) to a node / edit it. The joint joins the nearest self-or-ancestor rigid body of its node to that of the connected node (no connected node = the world).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_joint","arguments":{"scene_name":"Default Scene","node_name":"Door","connected_node_name":"Frame","settings_name":"Hinge","enable_collision":false}}}'
```

Parameters: node reference, `connected_node_id`/`connected_node_name` (optional), `settings_name` (optional, empty = free six-dof joint), `enable_collision` (default false). `edit_physics_joint` additionally takes `joint_index` (default 0, for nodes with several joints), `connect_to_world` (clear the connected node) and `rebuild` (re-capture joint frames from current node transforms).

### create_physics_material / edit_physics_material

Shared physics material in the content library.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_material","arguments":{"scene_name":"Default Scene","name":"Ice","static_friction":0.05,"dynamic_friction":0.02,"restitution":0.1,"friction_combine":"minimum"}}}'
```

Fields: `static_friction`, `dynamic_friction`, `restitution`, `friction_combine`, `restitution_combine` (`average`/`minimum`/`maximum`/`multiply`). Edits re-apply the material to all bodies using it. `edit_physics_material` also takes `new_name`.

### create_collision_filter / edit_collision_filter

Shared collision filter (collision-system lists).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_collision_filter","arguments":{"scene_name":"Default Scene","name":"Debris","collision_systems":["debris"],"not_collide_with_systems":["debris"]}}}'
```

Fields: `collision_systems`, `collide_with_systems` (allowlist), `not_collide_with_systems` (denylist, used when the allowlist is empty). In edit, lists supplied replace the existing lists and re-apply to all bodies using the filter.

### create_physics_joint_settings / edit_physics_joint_settings

Shared joint settings (per-axis limits + drives).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_joint_settings","arguments":{"scene_name":"Default Scene","name":"Hinge","limits":[{"linear_axes":[true,true,true],"min":0,"max":0},{"angular_axes":[false,true,false],"min":-1.57,"max":1.57},{"angular_axes":[true,false,true],"min":0,"max":0}]}}}'
```

`limits` entries: `linear_axes`/`angular_axes` (`[x, y, z]` booleans), `min`, `max` (absent = unbounded; min == max fixes the axis), `stiffness` (absent = hard limit), `damping`. `drives` entries: `type` (`linear`/`angular`), `mode` (`force`/`acceleration`), `axis` (0-2), `max_force`, `position_target`, `velocity_target`, `stiffness` (> 0 selects a position motor), `damping`. In edit, arrays supplied replace the existing ones and all joints using the settings are rebuilt automatically.

### wake_physics_bodies

Activate all dynamic rigid bodies in a scene (bodies enter the world deactivated).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"wake_physics_bodies","arguments":{"scene_name":"Default Scene"}}}'
```

## UI Driving Tools

Tools that inspect the editor's ImGui content and inject window input events,
so the menu- and mouse-driven entry points can be exercised headlessly. The
run-book - the inspect / resolve / act loop, item addressing, the standing
traps - is [doc/agents/mcp_ui_driving.md](mcp_ui_driving.md). All
rectangles and all `x` / `y` arguments are editor window pixels with the
origin at the top left, the space `get_viewports` and `capture_screenshot`
use.

| Tool | Arguments | Result |
|------|-----------|--------|
| `get_imgui_hosts` | - | The ImGui contexts: name, default flag, visibility, display size, window count, recorded-frame flag, `hook_calls_total` |
| `get_imgui_windows` | `host` | Per window: `name`, `display_name`, `id`, rectangle, `active`, `hidden`, `collapsed`, `child`, `docked`, `dock_id`, `focused` |
| `get_imgui_items` | `host`, `window`, `label_contains`, `visible_only`, `limit` | Items of one recorded frame: `id`, `label`, `display_label`, `window`, rectangle and center, `index`, `status` |
| `get_imgui_item_rect` | `host`, `window`, `label` or `id`, `index`, `visible_only` | One item's rectangle, center, `status`, `match_count` |
| `imgui_click` | item selector, `button`, `modifiers`, `double` | Clicks the resolved item's center; returns `target` |
| `imgui_hover` | item selector, `modifiers` | Leaves the pointer on the item; returns `target` |
| `imgui_scroll` | `window` or item selector, `dx`, `dy`, `modifiers` | Turns the wheel over it; returns `target` |
| `mouse_click` | `x`, `y`, `button`, `modifiers`, `double` | Press and release without moving |
| `mouse_drag` | `from`, `to`, `button`, `modifiers`, `frames`, `hold` | Press, interpolated moves, release unless held |
| `mouse_release` | `button` | Ends a held drag |
| `mouse_wheel` | `x`, `y`, `dx`, `dy`, `modifiers` | Wheel at a point; positive `dy` zooms in over a viewport |
| `key_press` | `key`, `modifiers`, `hold_frames` | A key chord as real input |
| `type_text` | `text` | UTF-8 text events to whatever holds keyboard focus |
| `inject_input_events` | `events` | Raw `erhe::window::Input_event` values with per-event `frame` offsets |
| `get_input_state` | - | Injected pointer position, held buttons, modifiers, the stepping gesture |
| `get_transform_handles` | `viewport` | Per gizmo handle, a window point that picks it, its `handle_value`, the gizmo anchor and radius |

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"imgui_click","arguments":{"window":"Operations","label":"Merge"}}}'
```

`capture_screenshot` takes `annotate_imgui_items` (plus `annotate_window` and
`annotate_limit`), which draws numbered rectangles over the recorded items and
returns the number -> item table.

## Notes

- `get_node_details` includes `brush_name`, `brush_id`, `locked`, `tags`, and mesh `vertex_count`/`facet_count`
- `get_scene_nodes` includes `locked` and `tags` fields per node
- `get_scene_brushes` includes `geometry_state` per brush, and `vertex_count` / `facet_count` for the brushes that are `ready`
- Brush instance scale is baked into the geometry at placement time and not stored separately - it cannot be queried back from existing nodes
- Operations that fail set an `error` field visible in `get_undo_redo_stack`

## Editor Command Tools

All registered editor commands are also exposed as tools (undo, redo, clipboard operations, scene commands, etc.). These take no arguments:

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"Undo","arguments":{}}}'
```

## Threading Model

The HTTP server runs on a dedicated background thread. All `tools/call` requests are placed in a thread-safe queue and the HTTP handler blocks on a `std::future`. On the main editor thread, `process_queued_requests()` is called each frame, drains the queue, dispatches to the appropriate handler (query or command), and sets the promise to unblock the HTTP response.

## Configuration

The server listens on `127.0.0.1:3743` by default (localhost only). The preferred port can be changed via the `Mcp_server` constructor parameter. If the preferred port is already bound, the server retries the next ports in sequence - up to 20 attempts, i.e. the range `[3743, 3763)` - and binds the first that is free, logging `MCP server: listening on 127.0.0.1:<port>`. If all 20 are unavailable it logs `failed to bind any port in [3743, 3763)` and the server stays offline (the editor otherwise runs normally).

## Accessing the server on Quest / Android

On device the server binds the same loopback address (`127.0.0.1:3743`), which is not reachable from the host directly. Forward the port over adb and then talk to it exactly as on desktop:

```bash
# host -> device loopback (re-run after each device reconnect)
adb forward tcp:3743 tcp:3743
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"list_scenes","arguments":{}}}'
```

The Quest / Android APK declares the `INTERNET` permission (`android-project/app/src/main/AndroidManifest.xml`). Android gates **all** socket creation - even loopback `127.0.0.1` - behind this permission: an app not in the `AID_INET` group gets `EACCES` from `socket()`/`bind()` on **every** port. Without it the server's port-fallback loop logs `failed to bind any port in [3743, 3763)` (a blanket denial, not a port clash). The permission is granted at install; confirm with:

```bash
adb shell dumpsys package org.libsdl.app.quest | grep INTERNET
# -> android.permission.INTERNET: granted=true
```

## Source Files

- `src/editor/mcp/mcp_server.hpp` - Server class declaration
- `src/editor/mcp/mcp_server.cpp` - Implementation (queries + command dispatch)
- `src/editor/editor.cpp` - Startup/shutdown/tick integration

## Dependencies

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.18.7 - Single-header HTTP server (fetched via CPM)
- [nlohmann/json](https://github.com/nlohmann/json) - JSON serialization (already in project)
