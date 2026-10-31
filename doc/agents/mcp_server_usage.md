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
that have one - its transform.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_scene_nodes","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{nodes: [{name, id, type, parent, parent_id, locked, active, import_root, tags}]}`,
each transformable prim additionally carrying `position`, `rotation_xyzw`
and `scale`. `type` is the prim's class name (`Xform`, `Mesh`, `Camera`,
`Light`, `Scope`, ...); a prim outside `Xformable` - a `Scope` - has no
transform, and the prims below it are listed with it as their `parent`.

### get_node_details

Get detailed info for a specific prim including world position, local transform, the prim's own class section, children, and selection state. A `Mesh` prim carries a `mesh` section (materials, primitive and vertex counts, world AABB, layer diagnostics), a `Camera` prim a `camera` section (`exposure`, `shadow_range`) and a `Light` prim a `light` section (`light_type`, `color`, `intensity`, `range`); the key is `null` on a prim of another class. A node placed by a brush carries a `brush_placement` section (`brush_name`, `brush_id`, `facet`, `corner`), null on every other node. A node carrying a rigid body carries a `physics` section (`motion_mode`, `is_trigger`, `gravity_factor`, `physics_material`, `collision_filter`, `collision_mesh`, `collision_shape`, and the live body's `mass`, `is_active` and damping), null on every other node. `joints` lists the `Joint` child prims that name this prim as their first frame node (`name`, `id`, `connected_node`, `joint_settings`, `enable_collision`, `constraint`). `composition_arcs` lists the composition arcs the prim carries, in authored order (doc/erhe/item.md, "Composition arcs"): each entry carries `source_path`, `name`, `prim_path` (the prim a USD `references` arc named, empty for a glTF prefab), `kind` (`reference` or `payload`) and `variant_selections` (`path`, `set`, `variant` per entry); it is empty on every prim that carries no arc. `parent` is the prim's parent in the tree and `transform_parent` the nearest transformable ancestor its world transform composes with (they differ when a `Scope` sits between them). A prim outside `Xformable` answers with its `type`, place and children alone. Every entry carries `active`: the effective `Item_flags::active` bit, false for an inactive prim and for everything below one (doc/erhe/usd_compatibility_design.md X2).

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

The shadow pass counts (`shadow_draw_list_entry_count` /
`shadow_draw_list_culled_count`): under the `gpu` culling classification with
GPU-generated shadow commands ([culling.md](../plans/culling.md) phase 6)
the CPU counts nothing, and the counts are the GPU count readback of an
earlier, retired frame (frames in flight late, laid out by that frame's
passes), named by the top-level `shadow_draw_list_counts_frame_index`; zero
before the first readback. Compare them with the `cpu` counts in steady
state; `get_cull_classification_check` compares the same frame's GPU and CPU
counts per job.

### get_cull_classification_check

The gate of the draw lists' GPU culling classification and GPU command
generation ([culling.md](../plans/culling.md) phases 5 and 6,
[draw_list_renderer.md](../erhe/draw_list_renderer.md) Q6 "GPU
classification", "GPU command generation"): the render phase of the next
frame the scene's draw lists classify on the GPU copies the frame's GPU mask
region, and the counts and commands its command generation wrote for the
shadow and color passes, to host memory and keeps the frame's CPU masks and what the
CPU command writer produces from them per job; once the frame retired, every
(entry, view) bit of every published draw list is compared, and per job
the GPU count and the commands up to it. A new request replaces an earlier
one. Errors when the device has no GPU path or the setting is cpu
(`set_culling_classification` `mode=gpu` first; the check then waits for the
first frame published with it, so both calls can go in one batch) and waits
a few frames.

```bash
py -3 scripts/mcp_call.py get_cull_classification_check b64:<base64 of {"scene_name":"Bench Heavy"}>
```

Returns: `{scene, frame_index, entry_count, view_count, dispatch_count,
mismatch_count, gpu_time_ns, gpu_bounds_write_count, lists: [{list_index,
purpose, entry_count, mismatch_count, mismatches: [{entry, view, cpu,
gpu}]}]}`. `mismatch_count` 0 is the gate. `entry_count` counts the entries
of all lists, `view_count` the views of the frame's set (every view of the
frame, classified at once before the first draw), `dispatch_count` the
compute dispatches of the frame (1, or 0 for a scene without entries).
`gpu_time_ns` is the dispatch's `Gpu_timer` result, the latest completed
measurement; a dispatch that overlaps the previous work on the GPU measures
close to 0 (the timer counts work where it finishes).
With the PVS rule ([pvs.md](../editor/pvs.md)) both
mask sets carry it; `pvs_culled_cpu_count` and `pvs_culled_gpu_count` count
the bits it removed on each path (the CPU lanes' bits, the GPU's PVS culled
words) and `pvs_mismatch_count` the bits they disagree on (also in
`mismatch_count`).
The command generation adds `command_job_count` (one job per shadow view and
chunk of each drawn shadow list, and per color pass selection and chunk of
each color list the pass draws; 0 for a frame without either),
`command_color_job_count` (the color selections' jobs among them),
`command_count` (the commands the CPU writer produces over all jobs, the
drawn entries), `command_mismatch_count` (jobs whose GPU count or
commands differ; 0 is the gate), `command_gpu_time_ns` (the compaction's
`Gpu_timer` result), `command_views: [{view, cpu_drawn, gpu_drawn}]` (per
view the drawn entries over its jobs: what the CPU writer produces and the
GPU-written count) and `command_mismatches: [{job, view, list_index,
chunk_begin, cpu_count, gpu_count, command_mismatch_count}]` (the first few).
`get_draw_lists` reports `cull_classification` (what the last publish
recorded) and `gpu_bounds_write_count` (the list bounds ranges written to
the GPU bounds buffers; unchanged on an idle scene).

### get_light_cluster_check

The gate of the GPU light binning
([clustered_lights.md](../plans/clustered_lights.md) V2,
[scene_renderer.md](../erhe/scene_renderer.md) `Light_cluster_gpu_binner`):
the render phase of the first frame binned on the GPU whose render phase
starts after the request (at least one frame after the one being prepared)
bins every GPU-binned view's inputs on the CPU as well and copies the view's
GPU lists and overflow counters to host memory; once the frame retired,
every cluster's category counts and slots are compared. A new request
replaces an earlier one. Errors when the binning pipeline did not build,
the `light_cluster_binning` setting is cpu (`set_light_cluster_binning`
`mode=gpu` first) or no view was shaded clustered in the last frame (the
preset's `light_shading` is `loops`, or `automatic` with at most 16 active
lights; `set_graphics_preset` `light_shading=clustered` first); waits a few
frames.

```bash
py -3 scripts/mcp_call.py get_light_cluster_check
```

Returns: `{frame_index, view_count, mismatch_count, views: [{scene_view,
tiles_x, tiles_y, slices, cluster_count, binned_light_count, listed_count,
mismatch_count, cpu_overflow_count, gpu_overflow_count,
cpu_max_list_length, gpu_max_list_length, mismatches: [{cluster,
cpu_counts, gpu_counts, cpu_slots, gpu_slots, gpu_offset_wrong}]}]}`.
The gate is `mismatch_count` 0 with `view_count` >= 1 and `listed_count` > 0
in the views that matter (a scene without spot or point lights checks empty
lists). `mismatch_count` counts the clusters whose GPU list
differs, plus each view whose GPU overflow count or longest list differs
from the CPU's. `listed_count` is the list entries over all clusters.

### get_light_cluster_stats

The light cluster binning of each scene view (one shadow render node per
view; [clustered_lights.md](../plans/clustered_lights.md) C4, V6) as the
frame results of the last rendered frame carry it.

Returns: `{binning_setting, gpu_supported, light_shading, views:
[{scene_view, rendered_last_frame, light_shading, frame_index, binning, tiles_x, tiles_y,
slices, cluster_count, binned_light_count, capacity, overflow_cluster_count,
max_list_length, gpu_time_ns}]}`. With `gpu` binning `frame_index` names
the retired frame whose counters were read back (frames in flight earlier);
with `cpu` it is the frame itself. A view in loop mode or without a camera
reports the single-cluster grid (1 x 1 x 1). `overflow_cluster_count` > 0
also logs a warning in `editor.render`, once each time the count changes.
`gpu_time_ns` is the view's binning dispatch (`Gpu_timer` `Light cluster
binning <view>`, latest completed measurement).

### get_pvs_status

The scene's potentially visible set database
([pvs.md](../editor/pvs.md); `erhe::scene::Pvs_database`, saved as
[`ERHE_pvs`](../gltf_extensions/ERHE_pvs.md)).

Returns: `{scene_name, state, camera}`, `state` one of `absent` (no
database), `ready`, `stale`; `camera` is `{viewport, camera, position}` of
the first viewport showing the scene. With a database also `version,
scene_id, grid_origin, cell_size, cell_counts, cell_count, page_size,
page_dims, page_count, stored_pages, column_count, words_per_cell,
cells_with_pvs, bytes {columns, pages, bits, total}, bake {method,
face_resolution, tile_size, layer_count, face_fov, samples_per_face,
silhouette_tests, duration_seconds}`,
`bits_source {path, byte_offset, byte_length}` for a database read from a
file, and in `camera`: `cell` (`"outside"` outside the grid), `page`,
`local_cell`, `page_stored`, `has_pvs`, `pvs_size`. `bake` includes
`method` (`sampled` or `disocclusion`); `bits_hash` is the 64-bit FNV-1a of
the bits as 16 hex digits, equal for two equal bakes and across a save and
reload. The consumer ([pvs.md](../editor/pvs.md)):
`culling_enabled` (the `pvs_culling` setting), `frame_culling_active` (the
scene's draw lists published the last frame with the rule on: the setting on
and the database ready) and `views: [{view, tested, culled}]`, per viewport
of the scene drawn in the last rendered frame (`view` its settings key):
the entries the rule tested (inside the frustum, with bounds and a baked
column) and those it culled. `bits_hash` is computed once when the
database is made (`Pvs_database::get_bits_hash()`), the camera's `pvs_size`
reads the one cell (from the scene file when an open released the bits);
`bits_in_memory` tells whether the database holds them in memory (after a
bake, or a save onto its own file).

`streaming` is the PVS page streaming ([draw_list_renderer.md](../erhe/draw_list_renderer.md)
Q6 "PVS resident set and page table"; `Draw_list_scene::get_pvs_streaming_status()`):
`active` (a residency exists), `resident_pages_setting` and
`stream_radius_pages` (the settings, `set_pvs_streaming`), `generation` (a
new one per database or settings change), `page_count`, `stored_pages`,
`words_per_page`, `capacity` (the resident page slots), `resident_pages`,
`max_resident_pages` (the most at once, never above `capacity`),
`slot_bytes`, `uploads` and `evictions` (since the residency was made),
`last_frame_index` with `last_frame_uploads` and `last_frame_evictions`,
`max_uploads_per_frame` (at most 1), `pending_requests`, `read_in_flight`,
`queued_uploads` (placed pages the render phase has not written yet),
`failed_pages`, `applied_uploads` (pages the render phase wrote into their
slots), the residency check `nonresident_views` and `nonresident_culled`
(views of a frame whose PVS rule read a cell whose page table slot did not
hold that page, and the entries they culled: 0 is the gate),
`bits_in_memory`, and `views: [{view, active, last_frame, page_min,
page_max, wanted_pages, resident_pages, box_resident}]`, per view
registering a query box: its page box, the stored pages within the stream
radius of it and how many are resident, and whether every stored page the
box touches is resident (the rule can cull for the view). Settled after a
camera move: `box_resident`, `pending_requests` 0, no read in flight, no
queued uploads and `applied_uploads == uploads`.

```bash
python3 scripts/mcp_call.py get_pvs_status '{"scene_name":"Default Scene"}'
```

### pvs_bake, pvs_debug_face, get_pvs_bake_status, pvs_cancel, pvs_clear

The PVS bake (`Pvs_baker`, [renderers.md](../editor/renderers.md);
[pvs.md](../editor/pvs.md) "Bake"). `pvs_bake` starts a bake and returns at
once; the bake runs over frames and sets the database on the scene when
its last cell is done.

Arguments of `pvs_bake`: `scene_name`, `method` (`"sampled"`, the default,
or `"disocclusion"`), `cell_size`, `region_min` and `region_max` (both or
neither; they override the scene's `Scene_settings` `pvs_cell_size` and
`pvs_region_min` / `pvs_region_max` for this bake, and an unset region,
both corners zero, is the union of the static entries' world bounds),
`face_resolution` (512 sampled, 1024 disocclusion), `face_fov_degrees`
(120); sampled: `samples_per_cell` (9), `captures_in_flight` (8);
disocclusion: `layer_count` (64, at most 64), `tile_size` (8, a divisor of
`face_resolution`), `samples_per_face` (1; each further sample repeats the
face with a sub-pixel jitter of the projection), `silhouette_tests`
(false; rule 3 of [pvs.md](../plans/pvs.md) "Open"), `trace_cell`,
`trace_entries`, `trace_only` and `trace_face` (the bake trace below).
Mesh nodes the bake covers get a glTF uid when they have none. The
disocclusion method bakes one cell per frame (`capture_count` counts the
cells): it picks the cell's sample point (the first free one of the center
and the 26 points a quarter cell away; a cell without one gets no PVS),
renders six faces of `samples_per_face` depth passes each into the layered
buffer and runs the disocclusion passes, whose bounds vote forms the cell's
PVS: every entry whose world bounds reach into the cube [-z_near, z_near]^3
around the sample point or overlap a disoccluded chunk of a face's
90-degree core. For its duration the scene's database is the bake's column table (no
stored page: nothing is culled); an edit that stales it fails the bake,
and a bake that does not finish puts the previous database back.

The bake trace ([pvs.md](../editor/pvs.md) "Bake" 7): `pvs_bake` with
`trace_cell` (a cell index of the grid) and `trace_entries`
(`[[node_uid, primitive_index], ...]`); with `trace_only` true it bakes
only that cell of the grid. Given the full bake's region, cell size and
parameters, the traced cell gets the full bake's sample point and depth
range and reproduces the full bake's cell bit for bit (a region of only
that cell would not: its depth range differs). The trace
also runs the id pass and makes `get_pvs_bake_status` return `trace
{cell, sample_point, columns, faces}`: per traced column `in_cell_pvs`, and
per face and column the depth-pass `fragments`, the `samples` whose
`id_min` / `id_max` name it, `core_samples`, `disoccluded_samples`,
`in_column_words` (the cell's column words after this face),
`bounds_vote` (this face's vote alone), and the bounds' projection
(`near`, `in_face`, `rectangle`, `layers`, the middle `tile` and its
per-layer `tile_column {closed, disoccluded}`). `scripts/pvs_verify.py
--trace` buckets the misses with it. With `trace_face` (0..5) the bake also
copies that face's whole depth region to the host (256 MB at the
defaults), re-runs the face on the CPU with the silhouette tests on and
off, and returns `trace.diagnostics` (one line each): per traced entry
the first layer bit its chunk lacks, the windows of that layer whose cones
cover it without the tests, their rule 3 side decisions with the pixel
pair closest to a gap, and the surface at that pixel from the sample point
(raycast: mesh, primitive, double_sided, facing, cell corners behind its
plane).

`pvs_debug_face` shows the layered buffer of one face without a bake (from
the cell's sample point as the bake picks it, the center when it has no
free one; no disocclusion passes):
`scene_name`, `cell` (index in the grid `pvs_bake` with the same arguments
would use), `face` (0..5: +X -X +Y -Y +Z -Z), `layer`, `path` (prefix of
the two PNGs), and the grid and disocclusion arguments of `pvs_bake`. It
returns at once (status `running`, `debug_face` true); the job runs the
depth and id passes of that face once, reads `layer` back and writes
`<path>_depth.png` (the closest depth per pixel, black at the layer's front
depth to light gray at its back, white where the layer has no sample) and
`<path>_id.png` (`id_min` as a hashed color, black where empty), then puts
the scene's database back. When done, `get_pvs_bake_status` adds
`debug_cell`, `debug_face_index`, `debug_layer`, `debug_z_near`,
`debug_z_far`, `debug_layer_front`, `debug_layer_back`,
`debug_depth_entries` and `debug_id_entries` (entries the passes drew),
`debug_covered_pixels`, `debug_distinct_ids`, `debug_depth_path` and
`debug_id_path`.

`get_pvs_bake_status` returns `{state, scene_name, method, grid_origin,
cell_size, cell_counts, cell_count, column_count, cells_done, capture_count,
captures_done, unmatched_entries, uids_assigned, cells_with_pvs,
seconds_elapsed, seconds_remaining, debug_face, error}`, and for the
disocclusion method `sample_point_test` (false: some static entry has no
raytrace geometry and every cell samples from its center),
`cells_without_sample_point` and `gpu_ms {cell, depth_pass, id_pass,
tiles, layer_loop, map, vote}` (the latest measured cell: all its faces,
and its first face's passes; `id_pass` 0 outside a trace; kept after the
bake ends); `state` is `idle`,
`running`, `done`, `cancelled` or `failed` (`error` set). `pvs_bake`
replies with the same object plus the parameters used. `pvs_cancel` ends a
running bake (the scene keeps the database it had) and reports
`cancelled`. `pvs_clear` removes the scene's database.

```bash
python3 scripts/mcp_call.py pvs_bake '{"scene_name":"Default Scene","cell_size":2,"region_min":[-6,0.2,-1.5],"region_max":[6,4.2,6.5]}'
python3 scripts/mcp_call.py pvs_debug_face '{"scene_name":"Default Scene","cell_size":2,"region_min":[-6,0.2,-1.5],"region_max":[6,4.2,6.5],"cell":39,"face":5,"layer":41,"path":"logs/pvs_face"}'
python3 scripts/mcp_call.py get_pvs_bake_status '{}'
```

### get_pvs_cell

One cell of a scene's PVS database: `scene_name` and exactly one of `cell`
(index) or `position` [x, y, z] (inside the grid). Returns `{scene_name,
cell, coordinate, cell_min, cell_max, page, local_cell, page_stored,
has_pvs, pvs_size, columns}`, `columns` the cell's PVS as `[{node_uid,
node_name, primitive_index}]` (`node_name` of the live mesh with that uid,
`""` when none; empty for a cell without a PVS). With `all_columns` true
it also returns `all_columns`, every column of the database as `[{node_uid,
primitive_index}]` (the tracked entries; an entry without a column is
always kept).

```bash
python3 scripts/mcp_call.py get_pvs_cell '{"scene_name":"Default Scene","cell":0}'
```

### get_visible_entries

The set of mesh primitives seen from an explicit camera pose, the exact
from-point visibility a PVS must contain ([pvs.md](../editor/pvs.md)
"Verification"; design in [editor.md](../editor/editor.md) "Whole-image id
capture"). Renders the scene's id pass over a whole offscreen image and reads
it back; independent of the viewport windows; spans several frames like
`render_scene_image` and cannot run inside `batch`.

Arguments: `scene_name`, `position` [x, y, z], exactly one of `target`
[x, y, z] (with `up`, default [0, 1, 0]) and `orientation` [x, y, z, w] (the
camera looks down its local -Z), `fov_y_degrees` (60), `width` / `height`
(1024 x 1024, at most 8192), `near` (0.03), `far` (200), `scene_only` (true:
content layer only; false: also the rendertarget and bone layers, as the
viewport id pass).

Returns: `{scene_name, width, height, scene_only, camera, frame_index,
covered_pixels, unresolved_pixels, entry_count, entries}`, `entries` a list of
`{node_uid, node_name, primitive_index, mesh_name, pixels}` sorted by
`node_uid` then `primitive_index`; `node_uid` is the mesh node's glTF uid,
`""` for a node without one. `covered_pixels` counts the pixels with any id;
`unresolved_pixels` the ids that name no live mesh primitive (a mesh
destroyed while the capture was in flight).

```bash
python3 scripts/mcp_call.py get_visible_entries '{"scene_name":"Default Scene","position":[0,1.3,8],"orientation":[0,0,0,1]}'
```

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

### get_viewports

Lists every viewport: each viewport window (the four view's four panes are
four windows) with `kind` `window`, and, when the OpenXR session runs, the
headset view with `kind` `headset` and `title` `headset`. The `title` is the
name the `viewport` argument of other tools takes.

Returns: `{viewports: [{kind, title, scene, camera, render_width,
render_height, msaa_sample_count, camera_path, camera_world_from_camera,
camera_projection_type, camera_fov_y, camera_fov_x, camera_z_near,
camera_z_far, ...}]}`. `render_width` / `render_height` are the size of the
image the viewport renders (a window's content render target; the headset's
per-view swapchain image). A window entry adds `x, y, width, height`, the
rectangle the image occupies in the editor window (top-left origin, the
pixel space of a `capture_screenshot` PNG and of the UI-driving tools). A
headset entry adds `active, view_count, multiview, views [{width, height}],
color_format`.

### get_viewport_size

The size of one viewport's image: `viewport` is a window title or
`headset` (default: the first viewport window).

Returns: `{viewport, kind, render_width, render_height, msaa_sample_count}`
plus `window_rect {x, y, width, height}` for a window, or `view_count,
multiview, views, color_format, active` for the headset. A build without
OpenXR refuses `headset` with that reason.

### set_overdraw_visualization

A viewport's overdraw visualization (`doc/plans/overdraw_visualization.md`):
`mode` (required: `off`, `shaded` or `rasterized`), `viewport` (window
title; default: the first viewport window with a scene view), `max_count`
(the count shown white, default 8, kept per viewport). Not off, the
viewport's content passes count per pixel with the viewport's real
pipelines, draw order and draw-list / culling paths, and the viewport shows
the counts as a heat map with a legend. `shaded` counts the fragment shader
invocations the frame runs (after the early depth test); `rasterized` counts
every rasterized fragment, ignoring the depth test - the mode that shows what
occlusion and frustum culling remove. Refused with the reason on a device
without fragment stage buffer atomics.

Returns: `{viewport, mode, max_count}`.

### get_overdraw_stats

The counts of the newest retired frame of a viewport whose overdraw
visualization is not off (`viewport` as above), a few frames behind the
current one: `{viewport, mode, counted_mode, valid, frame_index, width, height,
invocations_total, covered_pixels, mean_per_covered_pixel, mean_per_pixel,
max_count, histogram, histogram_last_bin}`. `histogram[n]` is the pixel
count with n invocations for n < `histogram_last_bin` (64); the last entry
holds every larger count. `valid` is false (with a `message`) until a counted
frame retired, and after the mode turned off. `counted_mode` is the mode
the reported frame counted with; after a mode change, wait until it equals
`mode`. With MSAA, the pixels a
triangle edge splits count once per covering triangle.
`scripts/overdraw_verify.py` checks the counts against stacked full-screen
rectangles.

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

### Saving scenes: save_scene, save_scene_for_quest, get_save_status

Both save tools are asynchronous. The tool validates its arguments (unknown
scene, a USD-backed scene for Save for Quest), queues the save and returns
the job at once: `{job_id, tool, scene_name, path, format, state: "pending",
saved: false, message}`. The save runs on the main thread at the end of the
same request pass, after that pass's replies are sent, through the same
save functions File > Save uses; it holds the main thread for its whole run
(seconds on a large scene), so other tools wait for it, while
`get_save_status` does not: the server thread answers it directly from the
job table. Poll `get_save_status {job_id}` until `state` is `done` or
`failed` before reading the file.

`get_save_status {job_id}` returns that job: `state` is `pending`,
`running`, `done` or `failed`. A done job adds `saved: true`, `file_bytes`,
`save_ms` and, for a glTF save, `export_ms` and `prepared{prepared_count,
retained_count, skipped_count, skip_reasons, bvh_reused_count,
bvh_rebuilt_count, bvh_stored_count, bvh_none_count,
dropped_degenerate_triangles, preparation_ms}` and `sources{overwrote_sources,
rebound_primitive_count, detached_primitive_count, rebound_image_count}`
(a save onto the scene's own source file, doc/editor/scene_serialization.md
"Saving onto the source file"); Save for Quest adds
`textures{image_count, transcoded_count, transcode_failed_count,
raw_ktx2_count, source_byte_count, transcoded_byte_count, transcode_ms}`. A
failed job carries `error` (a scene closed before its save ran, a write
failure; details in `logs/log.txt`). Without `job_id` the tool returns
`{jobs: [...]}`, the most recent 64 jobs.

The file is written through a temporary file in the target directory and
replaces the target only once complete (`erhe::file::write_file_via_temp`),
so a save onto the scene's own source works and a failed save leaves the
old file untouched.

`save_scene` writes the scene in its own format (the scene persistence
path, File > Save Scene); `set_as_source` binds the scene to `path` once the
save succeeds.

`save_scene_for_quest` ("Save for Quest", File > Save glTF for Quest)
writes a glTF scene like `save_scene` (prepared mesh data, editor state),
but every Basis Universal KTX2 texture is transcoded at save time to a raw
ASTC 4x4 KTX2 image referenced through `ERHE_texture_ktx2`
(`doc/gltf_extensions/ERHE_texture_ktx2.md`; levels Zstandard
supercompressed, or stored as they upload with
`"texture_supercompression": "none"`), and each prepared primitive carries
its GPU-ready vertex streams (`ERHE_gpu_vertex_streams`); PNG / JPEG images
are written as they are. The save status reports the streams in
`prepared` (`gpu_vertex_stream_count`, `gpu_vertex_stream_skipped`,
`gpu_vertex_stream_ms`). An export: the scene's source binding and
dirty state stay. A USD-backed scene is refused. `path` defaults to
`res/editor/scenes/<scene name>_quest.glb`; `.glb` is appended when the
extension is neither `.glb` nor `.gltf`.

```bash
py -3 scripts/mcp_call.py save_scene_for_quest '{"scene_name":"bistro_lights_c.gltf","path":"logs/bistro_quest.glb"}'
# -> {"job_id": 1, "state": "pending", ...}
py -3 scripts/mcp_call.py get_save_status '{"job_id":1}'
# -> {"job_id": 1, "state": "done", "file_bytes": ..., "textures": {...}, ...}
```

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

### set_graphics_preset

Set shadow fields of the graphics preset in effect, and the draw-list gate,
for the rest of the editor run: `shadow_filter`, `shadow_bias`,
`shadow_technique`, `shadow_cull_mode` (enum value names, e.g. `hard`,
`receiver_plane`, `distance`, `cull_back`), `shadow_depth_bits` (a requested
bit count, as in `graphics_presets.json`: any positive value, resolved by
`choose_shadow_depth_format()` to the nearest supported depth format,
preferring more bits), `shadow_resolution`, `point_shadow_resolution`,
`shadow_depth_bias_constant`, `shadow_depth_bias_slope`,
`shadow_bias_texel_scale`, `shadow_bias_origin_scale` (the minimum bias scales
of [shadows.md](../erhe/shadows.md) "Minimum bias", numbers >= 0),
`shadow_enable` (false: no shadow maps, every light unshadowed), the per
light type limits `directional_shadow_light_count`,
`directional_unshadowed_light_count`, `spot_shadow_light_count`,
`spot_unshadowed_light_count`, `point_shadow_light_count`,
`point_unshadowed_light_count` (integers >= 0; their total is clamped to the
renderer's `max_light_count`), `light_shading` (`loops` | `clustered` |
`automatic`: clustered shades every active light from per-fragment light
cluster lists and ignores the unshadowed counts; automatic, the default, is
clustered while the scene has more than 16 active lights, else loops;
[shader_variants.md](../erhe/shader_variants.md) "Light shading"), `specular_anti_aliasing` (`off` | `normal_kernel` |
`finite_difference`: the anisotropic BRDF's specular anti-aliasing,
[scene_renderer.md](../erhe/scene_renderer.md) "Specular anti-aliasing"),
`use_draw_lists`. Omitted arguments keep their values, so `{}` queries. The edited preset goes
through `Graphics_settings::apply_preset()`, the same apply as a Settings
window preset edit (limits clamp; the shadow maps are reconfigured on the next
frame, logged as `Reconfiguring N shadow render node(s) from preset ...`). The
change is session-only: neither `graphics_presets.json` nor
`editor_settings.json` is modified, and the next Settings window preset edit or
selection re-applies the stored preset. An unknown enum name or unsupported
value is an error and applies nothing.

```bash
py -3 scripts/mcp_call.py set_graphics_preset b64:<base64 of {"shadow_filter":"hard","shadow_depth_bias_constant":-4,"shadow_resolution":1024}>
```

Returns: every field above with the value in effect, plus `preset_name`,
`shadow_map_format` (the depth format the requested
`shadow_depth_bits` resolves to, e.g. `format_d32_sfloat` for 24 on a device
without a 24-bit format) and `shadow_depth_bits_axis` (that format's
`ERHE_SHADOW_DEPTH_BITS` variant value: 16 / 24 UNORM, 32 float).

### set_culling_classification

Set `Editor_settings_config::culling_classification` (the live setting, as
the Settings window's Culling Classification combo edits it; saved only by a
Settings window save): `mode` `cpu` or `gpu`
([culling.md](../plans/culling.md) phases 5 and 6; `gpu` classifies on
the GPU and generates the shadow passes' draw commands there). The next
frame's render
settings snapshot carries it to the draw lists' publish; a device without the
GPU path's capability keeps the CPU path (`get_draw_lists`
`cull_classification` reports what was published). Omit `mode` to query.

Returns: `{mode, gpu_supported}`.

### set_occlusion_culling

Set `Editor_settings_config::occlusion_culling` (the live setting, as the
Settings window's Occlusion Culling checkbox edits it; saved only by a
Settings window save): `enabled` true or false
([occlusion_culling.md](../plans/occlusion_culling.md)). With the `gpu`
culling classification on a device whose classifier supports the test
(fragment shader storage atomics), every viewport's "Occlusion test"
composition pass rasterizes the draw lists' entry boxes against the view's
opaque depth and marks the visible entries, and the next frame's
classification removes the entries it found hidden from the view's GPU
color commands (one frame of lag; a static view renders the same image).
Omit `enabled` to query.

Returns: `{enabled, classification, gpu_supported}`.

### set_pvs_culling

Set `Editor_settings_config::pvs_culling` (the live setting, as the Settings
window's PVS Culling checkbox edits it; saved only by a Settings window
save): `enabled` true or false ([pvs.md](../editor/pvs.md)). With a ready database, both culling classification paths
cull from each viewport's color view the baked entries whose bit is clear
in every grid cell holding a corner of the view's query box (the camera
position plus and minus `pvs_query_margin`, default 0.05 m; an orthographic
camera has none). An absent or stale database culls nothing; previews,
thumbnails, `render_scene_image`, the id pass, the shadow passes and the
headset get no PVS culling. Takes effect at the next frame's render settings
snapshot. Omit `enabled` to query. Counts: `get_pvs_status` `views`,
`get_composition_passes` `last_pvs_tested_count` / `last_pvs_culled_count`.

Returns: `{enabled, query_margin, classification}`.

### set_pvs_streaming

Set `Editor_settings_config::pvs_resident_pages` and
`pvs_stream_radius_pages` (the live settings, as the Settings window's PVS
Resident Pages and PVS Stream Radius entries edit them; saved only by a
Settings window save) and push them to every scene's draw lists at once
([draw_list_renderer.md](../erhe/draw_list_renderer.md) Q6 "PVS resident set and page table"): `resident_pages`
(1 to 65536, default 64) page slots hold PVS pages on the GPU and in the CPU
lanes' copy; `stream_radius_pages` (0 to 64, default 1) is the radius, in
pages, around a viewport camera's page that its query box registration
requests when the camera enters another page. A change remakes the resident
set (the pages stream in again, one per frame); a page without a slot reads
as everything visible. Omit both to query. Progress: `get_pvs_status`
`streaming`.

Returns: `{resident_pages, stream_radius_pages}`.
### set_content_draws

Set which content draws the composition passes make, by the blending class
of the primitive's material (`src/editor/renderers/content_draws.hpp`;
session only, never saved; the Settings window's Content Draws combo):
`mode` `all` or `opaque_only`. `opaque_only` leaves out the alpha-tested
content passes (`alpha_test` and `screen_door` materials, the draws whose
fragment shader discards) and the translucent content passes (`alpha_blend`
and the other blended modes) in every view, viewports and the headset, to
measure what those draws cost. Shadow passes draw opaque casters only in
both modes. Takes effect at the next frame's render settings snapshot. Omit
`mode` to query. `scripts/content_draws_verify.py` checks it with the
rasterized overdraw count.

Returns: `{mode}`.

### set_viewport_style

Set the Visual Style toggles of every viewport scene view, as the toolbar
Visual Style popup's checkboxes do: `style` `not_selected` (default) or
`selected`, and any of `edge_lines`, `corner_points`, `polygon_centroids`,
`solid_wireframe` (booleans; omitted keeps the value). Turning on a mode only
a full mesh build provides queues the on-demand full build of every prepared
primitive in every scene (doc/editor/async_asset_loading.md "Prepared mesh
data"), exactly as the checkbox does; wait for `get_async_status` idle before
capturing. `render_scene_image` draws no edge lines; capture them with
`capture_screenshot` of a viewport.

Returns: `{style, edge_lines, corner_points, polygon_centroids, solid_wireframe, full_builds_queued}`.

### set_headset_scene

Home the global tools (Hud, Hotbar and, under OpenXR, the headset view) in
the open scene `scene_name`, so the headset renders that scene; the same as
the headset window's scene combo (`doc/editor/tools.md` "Headset scene").
The move is queued and completes on a following frame; the log line
`Global tools homed in scene '<name>'` marks it. Closing the scene the tools
are homed in (`close_scene`) moves them to the most recently opened
remaining scene without this call. On Quest, the headset passes'
`last_mesh_count` in `get_composition_passes` and the `XR multiview` timer
in `get_gpu_timers` show which scene the headset renders.

Returns: `{queued, scene_name}`; an error when no scene has that name.

### get_occlusion_check

The gate of the raster occlusion test: the next frame with occlusion views
keeps its CPU masks and entry identities; once the frame retired and the
frame reusing its slot decoded its occlusion words, the call returns per
occlusion view `bounded_in_frustum` (CPU masks), `tested`, `visible`,
`occluded`, `untested` (marked visible without a test), `near_plane` (boxes
reaching the near plane), `near_plane_occluded` (0 is the gate), `culled`
(entries that frame's classification removed because the frame before it
found them occluded) and
`first_occluded` `[{list_index, entry_index, name}]` (up to 16). Errors
unless `culling_classification` is `gpu` and `occlusion_culling` is on, on
a device with the test. `scripts/occlusion_wall_check.py` builds a wall with
boxes behind and in front of it, then a box around the camera, in a running
editor and checks the result. `get_cull_classification_check` reports
`occlusion_culled_count` and compares the masks with the culled bits added
back.

```bash
python3 scripts/mcp_call.py get_occlusion_check '{"scene_name":"Default Scene"}'
```

Returns: `{scene, frame_index, entry_count, view_count, views: [{view,
scene_view, bounded_in_frustum, tested, visible, occluded, untested,
culled, near_plane, near_plane_occluded, first_occluded}]}`. The counts of
the same test are also in `get_composition_passes` (`last_occlusion_*` of the
"Occlusion test" pass); they are decoded only for frames a
`get_occlusion_check` recorded, so they update only when a check runs
(`last_occlusion_counts_frame_index` names the frame, 0 before the first).

### set_light_cluster_binning

Set `Editor_settings_config::light_cluster_binning` (the live setting, as the
Settings window's Light Cluster Binning combo edits it; saved only by a
Settings window save): `mode` `gpu` (default; a compute pass per clustered
view, `res/shaders/light_cluster_bin.comp`) or `cpu` (the CPU binner on the
render thread) ([clustered_lights.md](../plans/clustered_lights.md) C4). The
next frame's render settings snapshot carries it to the shadow render
nodes; a device whose binning pipeline did not build bins on the CPU. Both
modes produce the same lists. Omit `mode` to query.

Returns: `{mode, gpu_supported}`.

### set_shadow_map_cache_enabled

Session-only switch of the shadow map cache
(`App_settings::shadow_map_cache_session`, default on; `doc/erhe/shadows.md`
"Shadow map cache"): `enabled` false draws every shadow pass every frame.
`get_scene_lights` reports per shadow-mapped light the passes the last
rendered frame kept: `shadow_cached` (2D layer, 0 / 1) and
`shadow_cube_cached_faces` (point cube, bit f = face f), and how often the
cache's owner has drawn each pass since the cache was created (cumulative):
`shadow_draw_count` (2D layer) and `shadow_cube_face_draw_counts` (six
counts, +X -X +Y -Y +Z -Z). A count that moved between two queries was
redrawn in between, whichever frames the queries read.
`scripts/shadow_map_cache_check.py` compares captures with it on and off.

Returns: `{enabled}`.

### set_shadow_fit_cache_enabled

Session-only switch of the shadow fit cache
(`App_settings::shadow_fit_cache_session`, default on; `doc/erhe/shadows.md`
"Shadow fit cache"): `enabled` false computes every shadow-mapped light's
caster-derived limits every frame. `get_scene_lights` reports per light
whether the last rendered frame reused them (`shadow_fit_reused`) and which
computation they are (`shadow_fit_serial`: equal in two queries means the
light was not refitted in between).
`scripts/shadow_map_cache_check.py` compares captures with it on and off.

Returns: `{enabled}`.

### set_shadow_maps_frozen

Measurement switch, session-only (`App_settings::shadow_maps_frozen_session`,
carried to the render phase by `Render_settings::shadow_maps_frozen`): with
`frozen` true the shadow render nodes record no shadow pass and the maps
keep what the last drawn frame left in them, while the light projections are
still fitted and published every frame. A run then measures what a perfect
shadow map cache would save (GPU time, render-thread time, cadence); the
shadows go stale if lights or casters move. `scripts/bistro_frame_bench.py
--mcp-before-settle 'set_shadow_maps_frozen={"frozen": true}'` measures
Bistro with it.

Returns: `{frozen}`.

### instantiate_prefab / reload_prefab / get_prefabs / set_prefab_template_property

`instantiate_prefab` places a prefab (a glTF file, or a USD file at a prim) into a scene as a carrier prim recording the composition arc, with the template's content cloned below it; `reload_prefab` re-reads a prefab's file and refreshes every instance, keeping their overrides; `get_prefabs` lists the loaded templates. `set_prefab_template_property` sets (or, with a null value, clears) a local value on an item INSIDE a template - `source_path`, optional `prim_path`, `item_path` (the M1 path below the template root), `property`, `value` - which no scene lookup reaches otherwise; every instance reads the change live through its reference layer. It is not undoable, like `reload_prefab`. An item inside an instance is edited with `set_item_property` (a local value there is an override; a null value clears it and exposes the template's value) and reports `"source": "reference"` in `get_item_properties` for what the template supplies.

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

### generate_lods / get_lod_info

Mesh level of detail on request (`doc/erhe/meshoptimizer_integration.md`
"Level of detail"). `generate_lods` takes `scene_name` and `node_ids`, or
`node_id` / `node_name`, of mesh nodes, plus either `settings` (the
`Lod_settings` fields: `level_count`, `ratio`, `attributes`
`positions_only` / `normals_and_colors`, `normal_weight`, `color_weight`,
`border` `free` / `locked`, `seams` `keep` / `permissive`, `fallback` `none`
/ `sloppy`, `normal_merge_angle`; omitted fields take the defaults) or
`clear: true`. It sets the request on every distinct primitive of the meshes
and queues the rebuild of their optimized variants. Returns `{mode, settings,
mesh_count, primitive_count, queued_count, task_count, unchanged_count,
held_count, not_optimized_count}`; poll `get_async_status` until `idle`.

`get_lod_info` takes `scene_name` and `node_id` / `node_name`. Returns
`{node, node_id, lod, draw_lists, primitives}`. `lod` is the mesh's
effective `Lod` record `{mode, forced_level, bias, min_level, max_level,
level}` (`erhe::scene::resolve_lod`; `level` is the forced level before the
chain clamp). `draw_lists` (null without draw lists) is `{registered, lod,
entries}`: the record the draw lists hold and, per entry, `{mesh_primitive_index,
purpose, variant, first_index, index_count, triangle_count, level}`, where
`level` is the chain level whose range the entry copied at registration (-1
when none matches). Per primitive `{primitive_index, name, prepared,
has_optimized_variant, optimization_hold, lod_settings (null when unset),
has_chain, generation_seconds, levels}`, each level `{level, first_index,
index_count, triangle_count, edge_count, error}` (`edge_count`: the level's
own edge lines, 0 for level 0, which draws the original variant's). A level
is forced with the property
tools: `set_item_property` `Lod.apply_lod` true, `Lod.mode` `Forced` and
`Lod.forced_level` on the mesh node or on a node above it.

## Physics Tools

Create and edit KHR_physics_rigid_bodies features: the rigid body values of a node, the `Joint` prims and the shared content-library items (physics materials, collision filters, joint settings). Creation tools queue undoable operations ("queued": true in the response - the object exists on the next editor frame). Edit tools apply immediately. Nodes are addressed by `node_id` (preferred) or `node_name`.

### get_physics_items

List the shared physics content-library items with full properties.

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"get_physics_items","arguments":{"scene_name":"Default Scene"}}}'
```

Returns: `{physics_materials: [...], collision_filters: [...], physics_joint_settings: [...]}`

### create_physics_body / edit_physics_body

Give a node a rigid body / edit it, by writing the node's `Node_physics.*` values (doc/erhe/property_system.md section 4.26). One rigid body per node. `get_node_details` reports the body under `physics` (`motion_mode`, `collision_shape`, `mass`, `is_trigger`, `physics_material`, `collision_filter`, ...).

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

### create_joint / edit_joint

Create a `Joint` prim below a node / edit it (`create_joint` / `edit_joint`). The joint joins the nearest self-or-ancestor rigid body of that node - its first frame node - to that of the connected node (no connected node = the world).

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"create_joint","arguments":{"scene_name":"Default Scene","node_name":"Door","connected_node_name":"Frame","settings_name":"Hinge","enable_collision":false}}}'
```

Parameters: node reference, `connected_node_id`/`connected_node_name` (optional), `settings_name` (optional, empty = free six-dof joint), `enable_collision` (default false). `edit_joint` additionally takes `joint_index` (default 0, for nodes with several joints), `connect_to_world` (clear the connected node) and `rebuild` (re-capture joint frames from current node transforms).

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
| `list_input_bindings` | `filter` | Per user-bindable command: input kind, default and effective bindings, `modified`, conflicts (`doc/editor/input_bindings.md`) |
| `set_command_bindings` | `command`, `bindings` | Replaces the command's bindings (`Binding_desc` text form, e.g. `key:ctrl+x`) |
| `reset_command_bindings` | `command` or `all` | Back to the default bindings |
| `get_transform_handles` | `viewport` | Per gizmo handle, a window point that picks it, its `handle_value`, the gizmo anchor and radius |

```bash
curl -X POST http://127.0.0.1:3743/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":"1","method":"tools/call","params":{"name":"imgui_click","arguments":{"window":"Operations","label":"Merge"}}}'
```

`capture_screenshot` takes `annotate_imgui_items` (plus `annotate_window` and
`annotate_limit`), which draws numbered rectangles over the recorded items and
returns the number -> item table.

## Image Tools

Two tools write images; choose by what is being verified.

- `render_scene_image` renders a scene offscreen through an explicit camera
  (`camera`: `eye`, `target`, `up`, `fov_y_degrees`, `near`, `far`,
  `exposure`, `shadow_range`; or `camera_node`: a scene camera's name or id)
  at an explicit `width` x `height` into a render target owned by the request.
  The image is independent of viewport windows and the ImGui layout, and
  editor overlays (grid, gizmos, selection outline, hover, debug
  visualizations) are never drawn. `output`: `png` (tonemapped, what a
  viewport shows) or `linear` (HDR scene color as `.pfm`, with luminance
  statistics in the reply). Options: `scene`, `path`, `shader_debug`,
  `shadow_debug_light` (the light, by name or id, that `shader_debug` 30
  shadow_visibility shows), `msaa_samples`, `color_format` (`rgba16f`
  default, `rgba32f` for `output: "linear"`). `shader_debug` 36
  world_position with `output: "linear"` reads back the per-pixel world
  position (use `rgba32f`: fp16 rounds by about 1 mm at 1 m). Linear
  debug renders mark uncovered pixels with NaN RGB (`background` in the
  reply). The reply's `shadow_maps` / `shadow_lights` give each
  shadow-mapped light's `texture_from_world` (row-major), map resolution
  and layer as that render's shadow pass used them. The forward render pass
  of each capture is timed by the GPU timer `render_scene_image forward
  pass`; `get_gpu_timers` lists every live GPU timer's latest result, which
  lands a few frames after the reply. Use it for scene-content verification: rendering changes,
  lighting, GI, A/B and reference comparisons. Design:
  `doc/editor/rendergraph.md` "Scene image capture".
- `capture_screenshot` captures the whole editor window as presented,
  ImGui included. Use it for UI verification (windows, widgets, gizmos,
  hover and selection feedback) and with `annotate_imgui_items`.

Both span the frames they need and cannot run inside `batch`.

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
