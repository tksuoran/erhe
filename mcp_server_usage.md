# erhe Editor MCP Server

The editor embeds an MCP (Model Context Protocol) server that exposes editor commands and scene/content-library queries over HTTP using JSON-RPC 2.0. The server starts automatically with the editor on `127.0.0.1:8080`. If that port is already in use it falls back to the next free port, scanning `[8080, 8100)`; the port it actually bound is logged as `MCP server: listening on 127.0.0.1:<port>`.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC 2.0 endpoint for MCP protocol |
| GET | `/health` | Health check, returns `{"status":"ok"}` |

## MCP Methods

All requests are JSON-RPC 2.0 POST to `/mcp`.

### initialize

Handshake - returns server info and capabilities.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"initialize"}'
```

### tools/list

List all available tools (query tools + editor commands).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"2","method":"tools/list"}'
```

### tools/call

Invoke a tool by name. All tools are queued for execution on the main editor thread (5-second timeout).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"3","method":"tools/call","params":{"name":"TOOL_NAME","arguments":{}}}'
```

## Query Tools

These tools query editor state and return structured JSON data.

### list_scenes

List all scenes with summary counts.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"list_scenes","arguments":{}}}'
```

Returns: `{scenes: [{name, node_count, camera_count, light_count, material_count}]}`

### get_scene_nodes

List all nodes in a scene with transform and attachment info.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_nodes","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{nodes: [{name, id, parent, position, rotation_xyzw, scale, attachment_types}]}`

### get_node_details

Get detailed info for a specific node including world position, local transform, attachments (with mesh materials, camera/light properties), children, and selection state.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_node_details","arguments":{"scene_name":"Default Scene","node_name":"Cube"}}}'
```

### get_scene_cameras

List all cameras in a scene.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_cameras","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{cameras: [{name, node, exposure, shadow_range}]}`

### get_scene_lights

List all lights in a scene.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_lights","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{lights: [{name, node, type, color, intensity, range}]}`

### get_scene_materials

List all materials in a scene's content library.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_materials","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{materials: [{name, base_color, metallic, roughness, emissive}]}`

### get_material_details

Get full material properties including texture presence flags.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_material_details","arguments":{"scene_name":"Default Scene","material_name":"Gold"}}}'
```

Returns: `{name, base_color, opacity, roughness, metallic, reflectance, emissive, unlit, has_base_color_texture, ...}`

### get_scene_brushes

List all brushes in a scene's content library.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_brushes","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{brushes: [{name, id}]}`

### get_selection

Get currently selected items.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_selection","arguments":{}}}'
```

Returns: `{items: [{name, type, id}]}`

### select_items

Select items by unique ID. Searches scene nodes, cameras, lights, materials, and brushes.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"select_items","arguments":{"scene_name":"Default Scene","ids":[716,720]}}}'
```

Returns: `{selected_count, items: [{name, type, id}]}`

Pass an empty `ids` array to clear selection. All query responses include `id` fields for use with this tool.

## Action Tools

### place_brush

Place a brush instance in a scene at a given world position.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
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

### toggle_physics

Toggle dynamic physics simulation on/off.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"toggle_physics","arguments":{}}}'
```

Returns: `{dynamic_physics_enabled: true/false}`

### lock_items / unlock_items

Lock or unlock items by ID. Locked items (`lock_edit` flag) cannot be deleted or have properties edited.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"lock_items","arguments":{"scene_name":"Default Scene","ids":[506]}}}'
```

### add_tags / remove_tags

Add or remove string tags on items by ID.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"add_tags","arguments":{"scene_name":"Default Scene","ids":[506],"tags":["important"]}}}'
```

### get_undo_redo_stack

Get the undo/redo operation history.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_undo_redo_stack","arguments":{}}}'
```

Returns: `{undo: [{description, error?}], redo: [{description, error?}], can_undo, can_redo}`

### get_async_status

Get pending/running async operation counts.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_async_status","arguments":{}}}'
```

Returns: `{pending, running}`

## Physics Tools

Create and edit KHR_physics_rigid_bodies features: rigid body / joint node attachments and the shared content-library items (physics materials, collision filters, joint settings). Creation tools queue undoable operations ("queued": true in the response - the object exists on the next editor frame). Edit tools apply immediately. Nodes are addressed by `node_id` (preferred) or `node_name`.

### get_physics_items

List the shared physics content-library items with full properties.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_physics_items","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{physics_materials: [...], collision_filters: [...], physics_joint_settings: [...]}`

### create_physics_body / edit_physics_body

Attach a rigid body (Node_physics) to a node / edit it. One rigid body per node. `get_node_details` reports the body state (`motion_mode`, `collision_shape`, `mass`, `is_trigger`, `physics_material`, `collision_filter`, ...).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
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
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_joint","arguments":{"scene_name":"Default Scene","node_name":"Door","connected_node_name":"Frame","settings_name":"Hinge","enable_collision":false}}}'
```

Parameters: node reference, `connected_node_id`/`connected_node_name` (optional), `settings_name` (optional, empty = free six-dof joint), `enable_collision` (default false). `edit_physics_joint` additionally takes `joint_index` (default 0, for nodes with several joints), `connect_to_world` (clear the connected node) and `rebuild` (re-capture joint frames from current node transforms).

### create_physics_material / edit_physics_material

Shared physics material in the content library.

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_material","arguments":{"scene_name":"Default Scene","name":"Ice","static_friction":0.05,"dynamic_friction":0.02,"restitution":0.1,"friction_combine":"minimum"}}}'
```

Fields: `static_friction`, `dynamic_friction`, `restitution`, `friction_combine`, `restitution_combine` (`average`/`minimum`/`maximum`/`multiply`). Edits re-apply the material to all bodies using it. `edit_physics_material` also takes `new_name`.

### create_collision_filter / edit_collision_filter

Shared collision filter (collision-system lists).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_collision_filter","arguments":{"scene_name":"Default Scene","name":"Debris","collision_systems":["debris"],"not_collide_with_systems":["debris"]}}}'
```

Fields: `collision_systems`, `collide_with_systems` (allowlist), `not_collide_with_systems` (denylist, used when the allowlist is empty). In edit, lists supplied replace the existing lists and re-apply to all bodies using the filter.

### create_physics_joint_settings / edit_physics_joint_settings

Shared joint settings (per-axis limits + drives).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_physics_joint_settings","arguments":{"scene_name":"Default Scene","name":"Hinge","limits":[{"linear_axes":[true,true,true],"min":0,"max":0},{"angular_axes":[false,true,false],"min":-1.57,"max":1.57},{"angular_axes":[true,false,true],"min":0,"max":0}]}}}'
```

`limits` entries: `linear_axes`/`angular_axes` (`[x, y, z]` booleans), `min`, `max` (absent = unbounded; min == max fixes the axis), `stiffness` (absent = hard limit), `damping`. `drives` entries: `type` (`linear`/`angular`), `mode` (`force`/`acceleration`), `axis` (0-2), `max_force`, `position_target`, `velocity_target`, `stiffness` (> 0 selects a position motor), `damping`. In edit, arrays supplied replace the existing ones and all joints using the settings are rebuilt automatically.

### wake_physics_bodies

Activate all dynamic rigid bodies in a scene (bodies enter the world deactivated).

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"wake_physics_bodies","arguments":{"scene_name":"Default Scene"}}}'
```

## Notes

- `get_node_details` includes `brush_name`, `brush_id`, `locked`, `tags`, and mesh `vertex_count`/`facet_count` for nodes with attachments
- `get_scene_nodes` includes `locked` and `tags` fields per node
- `get_scene_brushes` includes `vertex_count` and `facet_count` per brush
- Brush instance scale is baked into the geometry at placement time and not stored separately - it cannot be queried back from existing nodes
- Operations that fail set an `error` field visible in `get_undo_redo_stack`

## Editor Command Tools

All registered editor commands are also exposed as tools (undo, redo, clipboard operations, scene commands, etc.). These take no arguments:

```bash
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"Undo","arguments":{}}}'
```

## Threading Model

The HTTP server runs on a dedicated background thread. All `tools/call` requests are placed in a thread-safe queue and the HTTP handler blocks on a `std::future`. On the main editor thread, `process_queued_requests()` is called each frame, drains the queue, dispatches to the appropriate handler (query or command), and sets the promise to unblock the HTTP response.

## Configuration

The server listens on `127.0.0.1:8080` by default (localhost only). The preferred port can be changed via the `Mcp_server` constructor parameter. If the preferred port is already bound, the server retries the next ports in sequence - up to 20 attempts, i.e. the range `[8080, 8100)` - and binds the first that is free, logging `MCP server: listening on 127.0.0.1:<port>`. If all 20 are unavailable it logs `failed to bind any port in [8080, 8100)` and the server stays offline (the editor otherwise runs normally).

## Accessing the server on Quest / Android

On device the server binds the same loopback address (`127.0.0.1:8080`), which is not reachable from the host directly. Forward the port over adb and then talk to it exactly as on desktop:

```bash
# host -> device loopback (re-run after each device reconnect)
adb forward tcp:8080 tcp:8080
curl -X POST http://127.0.0.1:8080/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"list_scenes","arguments":{}}}'
```

The Quest / Android APK declares the `INTERNET` permission (`android-project/app/src/main/AndroidManifest.xml`). Android gates **all** socket creation - even loopback `127.0.0.1` - behind this permission: an app not in the `AID_INET` group gets `EACCES` from `socket()`/`bind()` on **every** port. Without it the server's port-fallback loop logs `failed to bind any port in [8080, 8100)` (a blanket denial, not a port clash). The permission is granted at install; confirm with:

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
