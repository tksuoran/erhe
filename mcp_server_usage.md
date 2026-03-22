# erhe Editor MCP Server

The editor embeds an MCP (Model Context Protocol) server that exposes editor commands and scene/content-library queries over HTTP using JSON-RPC 2.0. The server starts automatically with the editor on `127.0.0.1:8080`.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| POST | `/mcp` | JSON-RPC 2.0 endpoint for MCP protocol |
| GET | `/health` | Health check, returns `{"status":"ok"}` |

## MCP Methods

All requests are JSON-RPC 2.0 POST to `/mcp`.

### initialize

Handshake — returns server info and capabilities.

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
- `scene_name` (required) — target scene
- `brush_id` (required) — brush ID from `get_scene_brushes`
- `position` (required) — `[x, y, z]` world position
- `material_name` (optional) — material name, defaults to first available
- `scale` (optional) — scale factor, default 1.0
- `motion_mode` (optional) — `"static"` or `"dynamic"` (default)

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

## Notes

- `get_node_details` includes `brush_name`, `brush_id`, `locked`, `tags`, and mesh `vertex_count`/`facet_count` for nodes with attachments
- `get_scene_nodes` includes `locked` and `tags` fields per node
- `get_scene_brushes` includes `vertex_count` and `facet_count` per brush
- Brush instance scale is baked into the geometry at placement time and not stored separately — it cannot be queried back from existing nodes
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

The server listens on `127.0.0.1:8080` by default (localhost only). The port can be changed via the `Mcp_server` constructor parameter.

## Source Files

- `src/editor/mcp/mcp_server.hpp` — Server class declaration
- `src/editor/mcp/mcp_server.cpp` — Implementation (queries + command dispatch)
- `src/editor/editor.cpp` — Startup/shutdown/tick integration

## Dependencies

- [cpp-httplib](https://github.com/yhirose/cpp-httplib) v0.18.7 — Single-header HTTP server (fetched via CPM)
- [nlohmann/json](https://github.com/nlohmann/json) — JSON serialization (already in project)
