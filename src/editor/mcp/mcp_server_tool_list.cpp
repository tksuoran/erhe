// Mcp_server tool list: name / description / input schema for every tool.
// Split out of mcp_server.cpp; shares helpers via mcp_server_shared.hpp.

#include "mcp/mcp_server_shared.hpp"

namespace editor {

using namespace mcp_server_detail;

void Mcp_server::refresh_tool_list()
{
    std::lock_guard<std::mutex> lock{m_tools_mutex};
    m_tool_infos.clear();

    m_tool_infos.push_back({"batch", "Execute a list of tool calls in one request and one editor frame. Sub-calls run in order; each entry's result (unwrapped payload) and ok flag are reported in 'results'. Every operation the sub-calls record becomes ONE undo entry ('grouped_operations' reports how many were grouped). A failing sub-call does not stop the batch (error_count > 0 marks the whole response as an error, but earlier successes stay applied). batch cannot nest, and tools that need a rendered frame before answering (capture_screenshot in the windowed build) must be called on their own.", {
        {"type", "object"},
        {"properties", {
            {"calls", {
                {"type", "array"},
                {"minItems", 1},
                {"maxItems", 1024},
                {"description", "Tool calls to execute in order"},
                {"items", {
                    {"type", "object"},
                    {"properties", {
                        {"tool",      {{"type", "string"}, {"description", "Tool name (any tool except batch)"}}},
                        {"arguments", {{"type", "object"}, {"description", "Arguments for the tool (default {})"}}}
                    }},
                    {"required", json::array({"tool"})}
                }}
            }}
        }},
        {"required", json::array({"calls"})}
    }});

    // Query tools
    m_tool_infos.push_back({"list_scenes",         "List all scenes in the editor",                          schema_no_args()});
    m_tool_infos.push_back({"get_scene_nodes",     "List all nodes in a scene",                              schema_scene_name()});
    m_tool_infos.push_back({"get_composition_passes", "Inspect the Composer's composition passes and why each one drew or did not draw on its last render: name, enabled flag, whether it has a render-time is_enabled predicate, mesh layer ids, item filter bits, and last_result - one of never_rendered / disabled / is_enabled_false / no_scene_root / primitive_mode_disabled / no_mesh_layers / submitted, plus the scene view it last ran for and how many meshes its layers held. Use this when geometry is missing and a GPU frame capture shows NO draw calls and NO debug marker for the pass: a pass that returns early emits no marker at all, so the capture cannot tell a mis-gated pass from missing geometry. Note that 'submitted' only means the pass reached draw submission; its meshes may still have been rejected by the item filter.", schema_no_args()});
    m_tool_infos.push_back({"get_node_details",    "Get detailed info for a node (transform, attachments). A Mesh attachment reports world_aabb (world-space min/max) and skinned; for a skinned mesh the AABB is the POSED bounds derived from the joint transforms, unaffected by the mesh node's own transform (glTF requires skinning to ignore it).",  schema_scene_and_item("node_name", "Name of the node")});
    m_tool_infos.push_back({"get_scene_cameras",   "List all cameras in a scene",                            schema_scene_name()});
    m_tool_infos.push_back({"get_scene_lights",    "List all lights in a scene",                             schema_scene_name()});
    m_tool_infos.push_back({"get_scene_materials", "List all materials in a scene's content library",        schema_scene_name()});
    m_tool_infos.push_back({"get_material_details","Get detailed material properties",                       schema_scene_and_item("material_name", "Name of the material")});
    m_tool_infos.push_back({"get_scene_textures", "List all textures in a scene's content library",         schema_scene_name()});
    m_tool_infos.push_back({"get_scene_brushes",  "List all brushes in a scene's content library, each with its folder_path (slash-separated content-library folder hierarchy, '' = directly under the Brushes root)", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"scene_id",   {{"type", "integer"}, {"description", "Scene id (from list_scenes); use to disambiguate two scenes that share a name. Takes precedence over scene_name."}}}
        }}
    }});
    m_tool_infos.push_back({"get_viewports",        "List viewport windows: window title, bound scene name, and camera name. Use to verify which scene/camera each viewport shows (e.g. that opening a scene did not rebind pre-existing viewports).", schema_no_args()});
    m_tool_infos.push_back({"pick_at",              "Headless pick probe: arm the pointer at viewport pixel (x, y) and run the same hover update a real pointer runs, reporting every hover slot (content/tool/brush/rendertarget/grid/bone) plus 'nearest' - the hit resolved by the same slot-ownership rule a viewport click uses (bone slot replaces content in bone selection mode; a bone hit reports the joint a click would select). The id-render readback is ASYNC: the first call at a position returns raytrace hits only; the armed position keeps the id pass rendering there, so call again after a few frames for the merged result that includes id-picked (e.g. skinned) meshes.", {
        {"type", "object"},
        {"properties", {
            {"x",        {{"type", "number"}, {"description", "Viewport pixel x (origin bottom-left)"}}},
            {"y",        {{"type", "number"}, {"description", "Viewport pixel y (origin bottom-left)"}}},
            {"viewport", {{"type", "string"}, {"description", "Viewport window title (see get_viewports); default: first viewport with a scene view"}}}
        }},
        {"required", {"x", "y"}}
    }});
    m_tool_infos.push_back({"get_server_info",      "Get this editor MCP server's identity: name, version, process id (pid), build timestamp (compile time of the server), and bound port. Use it to detect a STALE editor: if the pid/build does not match the editor you just launched, another editor.exe is holding the port and your calls are hitting the wrong process.", schema_no_args()});
    m_tool_infos.push_back({"set_window_visibility", "Show or hide an editor ImGui window by its title (e.g. \"Inventory\"). Windows do per-frame work only while visible, so headless verification uses this to open windows the default layout leaves closed. On an unknown title the error payload lists all window titles.", {
        {"type", "object"},
        {"properties", {
            {"title",   {{"type", "string"},  {"description", "Window title, e.g. \"Inventory\""}}},
            {"visible", {{"type", "boolean"}, {"description", "true shows the window (default), false hides it"}}},
            {"focus",   {{"type", "boolean"}, {"description", "With visible=true, also select the window's dock tab and bring it to the front (default false). A merely visible window can sit behind another tab of the same dock node, invisible in screenshots."}}}
        }},
        {"required", {"title"}}
    }});
    m_tool_infos.push_back({"get_frame_pacing_status", "Frame pacing snapshot: display refresh period, pacer state (cadence N, margin, vsync grid phase), latest schedule decision, enforcement flag, simulated workload knob, and capture bounds (first/latest frame id). All times are seconds in the frame-record reference clock unless suffixed _ms.", schema_no_args()});
    m_tool_infos.push_back({"get_frame_pacing_frames", "Per-frame samples from the Frame Pacing window's persistent capture (decision + frame time record joined). Times are absolute reference-clock seconds; 0 = not (yet) recorded. Use for offline analysis: achieved present deltas vs refresh period, grid residuals, latency, clamp waits.", {
        {"type", "object"},
        {"properties", {
            {"first_frame_id", {{"type", "integer"}, {"description", "First frame id to return (default: latest - count + 1)"}}},
            {"count",          {{"type", "integer"}, {"description", "Number of frames (default 120, max 2000)"}}}
        }}
    }});
    m_tool_infos.push_back({"set_frame_pacing_min_vsyncs", "Set the frame pacer's application-commanded cadence floor N_min (FR1 set_min_vsyncs, behavior doc scenario 9). Raising above the current cadence downshifts immediately; lowering lets the normal upshift path (dwell + headroom) bring the cadence back down. Clamped to [1, 8]; applied at the next frame tick.", {
        {"type", "object"},
        {"properties", {
            {"min_vsyncs", {{"type", "integer"}, {"description", "Minimum display refresh periods per frame (1 = uncapped, default 1)"}}}
        }},
        {"required", {"min_vsyncs"}}
    }});
    m_tool_infos.push_back({"set_frame_pacing_workload", "Set the Frame Pacing window's simulated CPU workload (U2 sliders, headless twin): each frame busy-waits a duration drawn uniformly from [min_ms, max_ms] inside the measured CPU slot, so the pacer sees it as real load. Clamped to [0, 60] ms, min <= max. 0/0 disables. Use for induced-load scenarios (P3.1) and W-response checks.", {
        {"type", "object"},
        {"properties", {
            {"min_ms", {{"type", "number"}, {"description", "Minimum extra CPU time per frame in milliseconds (default 0)"}}},
            {"max_ms", {{"type", "number"}, {"description", "Maximum extra CPU time per frame in milliseconds (default min_ms)"}}}
        }},
        {"required", {"min_ms"}}
    }});
    m_tool_infos.push_back({"set_frame_pacing_capture", "Configure the Frame Pacing capture: max_frames caps how many frames are recorded (recording stops at the cap; 0 = unbounded) bounding memory and the capture-size-dependent UI draw cost; clear=true drops the capture. Set the cap and clear together for a bounded 'record a run' capture, then read it back with get_frame_pacing_frames.", {
        {"type", "object"},
        {"properties", {
            {"max_frames", {{"type", "integer"}, {"description", "Capture size cap in frames (0 = unbounded); recording stops when the capture reaches it"}}},
            {"clear",      {{"type", "boolean"}, {"description", "Drop the current capture (default false); recording restarts immediately"}}}
        }}
    }});
    m_tool_infos.push_back({"get_selection",        "Get currently selected items",                          schema_no_args()});
    m_tool_infos.push_back({"get_undo_redo_stack", "Get undo/redo operation stacks",                       schema_no_args()});
    m_tool_infos.push_back({"clear_undo_history",  "Drop the undo and redo histories (queued operations are kept). Recorded operations are declared users / indirect pins of the assets they retain, so container unload can refuse until the history is cleared (R5.4).", schema_no_args()});
    m_tool_infos.push_back({"get_async_status",   "Get in-flight operation counts: pending/running async workers plus queued_operations (queued on the operation stack, not yet executed on the main thread). The scene is settled only when all three are 0. Also carries a lightmap_prepare sub-object: in_flight/regions_done/regions_total/cancel_requested plus last_result{committed, mesh_count, piece_count, tile_count, abort_reason} of the most recent lightmap_prepare_tiles.", schema_no_args()});
    m_tool_infos.push_back({"get_transform_update_stats", "Per-frame cost of Scene::update_node_transforms() summed over all scenes (plus the tool scene): last frame's pass/dirty/visited counts and lock/sort/propagate CPU milliseconds, a running per-frame average since launch or the last reset (frames, avg_*, peak_total_ms), and per-scene registered node bucket sizes (transform_update_nodes / no_transform_update_nodes). Pass reset=true to zero the running aggregate AFTER reading (the response still reports the pre-reset aggregate) - use it to bracket a measurement window.", {
        {"type", "object"},
        {"properties", {
            {"reset", {{"type", "boolean"}, {"description", "Zero the running aggregate after reading (default false)"}}}
        }}
    }});
    m_tool_infos.push_back({"merge_static_subtree", "Bake the static geometry under a node into that node's own mesh, one combined primitive per material, and REMOVE the merged nodes - the runtime fix for deep visual-only part hierarchies riding on physics bodies (each node under a moving body costs per-frame transform propagation + a draw). Merges only nodes whose sole attachment is a Mesh; nodes with rigid bodies or no_transform_update (nested sway spines, sensor carriers) are boundaries - kept, and with recurse=true (default) each becomes its own merge segment, so one call on a tree root flattens the whole rig segment by segment. Attachment-less chain/pose nodes whose whole subtree merged are pruned too (attachment-less LEAF nodes such as joint pivot anchors always survive). Kept nodes whose parent was removed are reparented to the segment root world-preserving. Undoable. Setup cost is significant (geometry merge + GPU mesh build per material per segment); merged parts lose individual pickability.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Root node id (takes precedence over node_name)"}}},
            {"node_name",  {{"type", "string"},  {"description", "Root node name (alternative to node_id)"}}},
            {"recurse",    {{"type", "boolean"}, {"description", "Also merge each discovered boundary segment (nested spines) as its own target (default true)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_shadow_fit_debug","Dump directional shadow frustum fit debug geometry per shadow node: F_shadow planes, their bounded face quads (the truncated view-frustum faces caster AABBs are tested against), and receiver frustum corners. Needs the Shadow Fit 'Collect Debug' setting enabled.", schema_no_args()});
    m_tool_infos.push_back({"raycast",             "Shoot a ray through a scene's raytrace world and report the closest hit (mesh, node, primitive index, distance, position, normal). Uses the same mask defaults as the interactive viewport hover ray, so it verifies hover / ray picking behavior headlessly.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",   {{"type", "string"}, {"description", "Name of the scene"}}},
            {"origin",       {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Ray origin in world space [x, y, z]"}}},
            {"direction",    {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Ray direction [x, y, z] (normalized internally)"}}},
            {"max_distance", {{"type", "number"}, {"description", "Maximum hit distance (default 9999)"}}},
            {"mask",         {{"type", "integer"}, {"description", "Raytrace instance mask (default pickable_static: content|shadow_cast|tool|brush|rendertarget|controller|grid)"}}}
        }},
        {"required", json::array({"scene_name", "origin", "direction"})}
    }});
    m_tool_infos.push_back({"geometry_query",      "Run a BATCH of geometry queries in one call; per-query results come back in order (a query error fills that entry's 'error' without failing the batch). Types: 'raycast' {origin, direction, max_distance?, mask?} = closest raytrace-world hit exactly like the raycast tool; 'closest_point' {point, node_id? / node_name?} = closest point on render-mesh SURFACE triangles in world space (distance, position, triangle normal, owning node; searched over the named node's mesh, or EVERY mesh in the scene when no node is given - name a node for big scenes). Built for placement scripts: probe the ACTUAL surface (e.g. a convex hull bulges past its authored points) instead of guessing offsets, many probes per request.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"queries",    {{"type", "array"},  {"description", "Query objects: {type:'raycast', origin:[x,y,z], direction:[x,y,z], max_distance?, mask?} or {type:'closest_point', point:[x,y,z], node_id?, node_name?}"}, {"items", {{"type", "object"}}}}}
        }},
        {"required", json::array({"scene_name", "queries"})}
    }});
    m_tool_infos.push_back({"select_items",        "Select items by ID (scene nodes, materials, etc.). Mirrors UI selection semantics: replaces the selection within the target scene only (other scenes' selections persist; ids=[] clears the target scene only) and makes the target scene the active scene.",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to search for items"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Array of item IDs to select"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"delete_nodes",        "Delete nodes (whole SUBTREES) by id and/or name - the MCP counterpart of Edit > Delete, undoable through the same recursive-delete path, without touching the user's selection. lock_edit items and their subtrees survive (prefab instances always delete whole). Queued; settle before rebuilding into the freed names. Built for partial-rebuild iteration: delete one object's root group and rebuild only that object.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Node ids to delete (subtrees)"}}},
            {"names",      {{"type", "array"}, {"items", {{"type", "string"}}},  {"description", "Node names to delete (subtrees; exact match, all matches)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_active_scene",    "Get the active scene: the scene that commands targeting scene-hosted items act on (the transform gizmo, mesh operations, delete/cut/duplicate). Follows selection changes and window focus; null when no scene is active.", schema_no_args()});
    m_tool_infos.push_back({"set_active_scene",    "Make a scene the active scene, through the same activation path as focusing that scene's window in the UI (the gizmo rebinds to its selection, the window highlight moves, commands target it).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to activate"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"transform_selection", "Apply a Transform tool edit (translation / rotation / scale / skew) to the currently selected node(s), through the same code path as the Transform window numeric entry. space=local applies values in parent space (requires exactly one selected node); space=global applies in world (anchor) space. end_edit=true (default) records an undo operation and refreshes the edit baselines; end_edit=false keeps the edit session open like an active drag, so repeated calls re-apply against the same initial state.", {
        {"type", "object"},
        {"properties", {
            {"space",         {{"type", "string"},  {"description", "Edit space: 'local' or 'global' (default 'global')"}}},
            {"translation",   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation [x, y, z]"}}},
            {"rotation_xyzw", {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4}, {"description", "Rotation quaternion [x, y, z, w]"}}},
            {"scale",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Scale [x, y, z]"}}},
            {"skew",          {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Skew [x, y, z]"}}},
            {"end_edit",      {{"type", "boolean"}, {"description", "Record an undo operation and refresh edit baselines (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"place_brush",         "Place an instance of a content-library brush in a scene. THE shape-reuse path: every placement of one brush shares its geometry and GPU buffers, so create a shape once (create_shape with add_brush) and place it N times instead of calling create_shape N times. Placement parameters are identical to create_shape's instance parameters.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the scene"}}},
            {"brush_id",       {{"type", "integer"}, {"description", "Brush ID (from get_scene_brushes or create_shape add_brush; takes precedence over brush_name)"}}},
            {"brush_name",     {{"type", "string"},  {"description", "Brush name (alternative to brush_id)"}}},
            {"name",           {{"type", "string"},  {"description", "Instance node name (default: brush name). Renames only the node; the mesh keeps the brush name so instances stay export-deduplicable"}}},
            {"position",       {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}},
            {"rotation_xyzw",  {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 4}, {"maxItems", 4}, {"description", "World rotation quaternion [x, y, z, w] - avoids the select + transform_selection round trips"}}},
            {"parent_node_id", {{"type", "integer"}, {"description", "Parent node ID for the instance (default: scene root); the world position is preserved"}}},
            {"material_name",  {{"type", "string"},  {"description", "Material name (default: first available)"}}},
            {"material_id",    {{"type", "integer"}, {"description", "Material by unique item id; reaches any scene's materials and the asset manager's loaded container materials (takes precedence over material_name)"}}},
            {"scale",          {{"description", "Number = uniform brush bake scale (geometry, collision shape, volume and inertia all scale; use for physics parts; default 1.0). NOTE each distinct bake scale builds its own GPU primitive (shared by placements at that scale) - prefer few distinct scales, or the array form. Array [x, y, z] = node-space TRS scale composed into the transform (visual anisotropy, no new primitive; collision shapes do NOT follow node scale - pair with motion_mode 'none')"}}},
            {"mass",           {{"type", "number"},  {"description", "Rigid body mass override for the instance; local inertia is rescaled to match (default: brush density x volume). Only meaningful with a physics motion_mode"}}},
            {"motion_mode",    {{"type", "string"},  {"description", "Physics motion mode: static, kinematic, dynamic (default dynamic), or none = no rigid body at all (pure visual part, e.g. children of a physics-driven assembly)"}}},
            {"pose_node",      {{"type", "boolean"}, {"description", "Insert an extra rigid (position + rotation, NO scale) pose node and hang the scaled instance under it as a leaf. Node scale scales the whole subtree, so use this for any scaled part that will carry children; the returned node_id is the pose node (the safe attach point, also for joint carriers - it keeps the rotated frame), mesh_node_id is the scaled instance"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    m_tool_infos.push_back({"place_brush_instances", "Place MANY brush instances in one call (one request, one editor frame). Each placement takes the same parameters as place_brush (brush_id/brush_name, name, position, rotation_xyzw, scale, material_name/material_id, mass, motion_mode, pose_node), plus parent_index = index of an EARLIER placement in this same call to parent under it (its pose node when it used pose_node) - this is how chained structures (branch segments, nested parts) batch, since same-frame nodes are not findable by id. 'defaults' merges under every placement. On a mid-batch error the earlier placements stay applied and the error reports the failing index. Returns per-placement node ids in order.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"defaults",   {{"type", "object"}, {"description", "Placement fields merged under every entry (e.g. brush_id, material_name, motion_mode)"}}},
            {"placements", {{"type", "array"},  {"items", {{"type", "object"}}}, {"minItems", 1}, {"description", "Placements in order; each = place_brush parameters + optional parent_index"}}}
        }},
        {"required", json::array({"scene_name", "placements"})}
    }});

    m_tool_infos.push_back({"create_shape",        "Create a parametric shape using the erhe geometry generators, then place an instance in the scene and/or add the brush to the content library. Shape parameters: box (size [x,y,z], steps [x,y,z], power), uv_sphere (radius, slice_count, stack_count), cone (height, bottom_radius, top_radius, use_top, use_bottom, slice_count, stack_count), capsule (length, bottom_radius, top_radius, slice_count, stack_count; tapered when the radii differ, which requires length > |bottom_radius - top_radius|), torus (major_radius, minor_radius, major_steps, minor_steps), disc (outer_radius, inner_radius, slice_count, stack_count; annulus when inner_radius > 0; flat in the XY plane facing +/-Z), triangle (radius; flat XY), quad (edge; flat XY, double sided), rectangle (width, height, front_face, back_face; flat XY), regular_polyhedron (kind = tetrahedron / cube / octahedron / dodecahedron / icosahedron / cuboctahedron, radius; watertight - good CSG inputs), convex_hull (points = [[x,y,z], ...] in node-local space, >= 4 non-coplanar; watertight hull - THE way to get authored silhouettes like ship hulls; good CSG input). Flat shapes get a thin box collision shape - prefer motion_mode none for them.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the scene"}}},
            {"shape",          {{"type", "string"},  {"description", "Shape type: box, uv_sphere, cone, capsule, torus, disc, triangle, quad, rectangle, regular_polyhedron or convex_hull"}}},
            {"name",           {{"type", "string"},  {"description", "Brush / instance name (default: shape type)"}}},
            {"instance",       {{"type", "boolean"}, {"description", "Place an instance node in the scene (default true)"}}},
            {"add_brush",      {{"type", "boolean"}, {"description", "Add the brush to the content library for later place_brush use (default false)"}}},
            {"position",       {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] for the instance (default [0, 0, 0])"}}},
            {"parent_node_id", {{"type", "integer"}, {"description", "Parent node ID for the instance (default: scene root); the world position is preserved"}}},
            {"material_name",  {{"type", "string"},  {"description", "Material name (default: first available)"}}},
            {"material_id",    {{"type", "integer"}, {"description", "Material by unique item id; reaches any scene's materials and the asset manager's loaded container materials (takes precedence over material_name)"}}},
            {"rotation_xyzw",  {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 4}, {"maxItems", 4}, {"description", "World rotation quaternion [x, y, z, w] for the instance - avoids the select + transform_selection round trips"}}},
            {"scale",          {{"description", "Number = uniform brush bake scale (geometry, collision shape, volume and inertia all scale; use for physics parts; default 1.0). Array [x, y, z] = node-space TRS scale composed into the transform (visual anisotropy; collision shapes do NOT follow node scale - pair with motion_mode 'none'). For shape reuse prefer add_brush once + place_brush N times over N create_shape calls"}}},
            {"mass",           {{"type", "number"},  {"description", "Rigid body mass override for the instance; local inertia is rescaled to match (default: brush density x volume). Only meaningful with a physics motion_mode"}}},
            {"motion_mode",    {{"type", "string"},  {"description", "Physics motion mode for the instance: static, kinematic, dynamic (default dynamic), or none = no rigid body at all (pure visual part, e.g. children of a physics-driven assembly)"}}},
            {"size",           {{"type", "array"},   {"items", {{"type", "number"}}},  {"minItems", 3}, {"maxItems", 3}, {"description", "box: size [x, y, z]"}}},
            {"steps",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "box: subdivision steps [x, y, z]"}}},
            {"power",          {{"type", "number"},  {"description", "box: power"}}},
            {"radius",         {{"type", "number"},  {"description", "uv_sphere: radius"}}},
            {"height",         {{"type", "number"},  {"description", "cone: height"}}},
            {"length",         {{"type", "number"},  {"description", "capsule: cylinder section length"}}},
            {"bottom_radius",  {{"type", "number"},  {"description", "cone / capsule: bottom radius"}}},
            {"top_radius",     {{"type", "number"},  {"description", "cone / capsule: top radius"}}},
            {"use_top",        {{"type", "boolean"}, {"description", "cone: generate top cap (default true)"}}},
            {"use_bottom",     {{"type", "boolean"}, {"description", "cone: generate bottom cap (default true)"}}},
            {"slice_count",    {{"type", "integer"}, {"description", "uv_sphere / cone / capsule: slice count"}}},
            {"stack_count",    {{"type", "integer"}, {"description", "uv_sphere / cone: stack count; capsule: hemisphere stack count"}}},
            {"major_radius",   {{"type", "number"},  {"description", "torus: major radius"}}},
            {"minor_radius",   {{"type", "number"},  {"description", "torus: minor radius"}}},
            {"major_steps",    {{"type", "integer"}, {"description", "torus: major steps"}}},
            {"minor_steps",    {{"type", "integer"}, {"description", "torus: minor steps"}}},
            {"outer_radius",   {{"type", "number"},  {"description", "disc: outer radius (default 1)"}}},
            {"inner_radius",   {{"type", "number"},  {"description", "disc: inner radius; > 0 makes an annulus/ring (default 0)"}}},
            {"edge",           {{"type", "number"},  {"description", "quad: edge length (default 1)"}}},
            {"width",          {{"type", "number"},  {"description", "rectangle: width (default 1)"}}},
            {"front_face",     {{"type", "boolean"}, {"description", "rectangle: generate the +Z-facing face (default true)"}}},
            {"back_face",      {{"type", "boolean"}, {"description", "rectangle: generate the -Z-facing face (default true)"}}},
            {"kind",           {{"type", "string"},  {"description", "regular_polyhedron: tetrahedron, cube, octahedron, dodecahedron, icosahedron or cuboctahedron (default icosahedron)"}}},
            {"points",         {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}}, {"description", "convex_hull: node-local points [[x,y,z], ...], at least 4 non-coplanar"}}}
        }},
        {"required", json::array({"scene_name", "shape"})}
    }});
    m_tool_infos.push_back({"set_node_transform",  "Set a node's transform directly by node id/name - no selection involved (no gizmo rebind, no kinematic hold on selected dynamic bodies). ABSOLUTE set semantics: each provided component (translation / rotation_xyzw / scale) replaces that component of the node's current transform in the requested space; omitted components are preserved. All provided components apply in ONE call (unlike transform_selection's one-component-per-call drag emulation). Applied immediately and recorded as an undoable operation; the node's rigid body is snapped (teleported, no impulse) to the new pose. Note: a node created in the same frame attaches on the next frame and is not yet findable - retry on 'Node not found'.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "Node ID (takes precedence over node_name)"}}},
            {"node_name",     {{"type", "string"},  {"description", "Node name (alternative to node_id)"}}},
            {"space",         {{"type", "string"},  {"description", "'world' (default; 'global' accepted as alias) or 'local' (parent space)"}}},
            {"translation",   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation [x, y, z]"}}},
            {"rotation_xyzw", {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 4}, {"maxItems", 4}, {"description", "Rotation quaternion [x, y, z, w]"}}},
            {"scale",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Scale [x, y, z]"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_node",         "Create an empty scene node (undoable), optionally parented and positioned. Useful as a physics joint anchor: create_physics_joint captures its joint frames from the joint / connected node world transforms, so coincident anchor child nodes on the two bodies give a clean joint pivot.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new node (default 'new empty node')"}}},
            {"parent_node_id",   {{"type", "integer"}, {"description", "Parent node ID (default: scene root)"}}},
            {"parent_node_name", {{"type", "string"},  {"description", "Parent node name (alternative to parent_node_id)"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_light",        "Create a light (directional, point or spot) attached to a new scene-root node (undoable, inserted on the next frame). Point/spot lights default to range 25; directional to range 0.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "Light type (default directional)"}}},
            {"name",             {{"type", "string"},  {"description", "Name for the new light / node (default 'MCP light')"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] (default [0, 0, 0])"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB color [r, g, b] (default [1, 1, 1])"}}},
            {"intensity",        {{"type", "number"},  {"description", "Light intensity (default 1.0)"}}},
            {"range",            {{"type", "number"},  {"description", "Light range / far distance (default 25 for point/spot, 0 for directional)"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows (default true)"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians (spot only)"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians (spot only)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_light",          "Edit an existing light in place (by light_id or light_name). Changing 'type' re-buckets the light for shadow rendering. 'position' moves the light's node. Only the provided fields change.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",       {{"type", "string"},  {"description", "Name of the scene"}}},
            {"light_id",         {{"type", "integer"}, {"description", "ID of the light to edit (from get_scene_lights)"}}},
            {"light_name",       {{"type", "string"},  {"description", "Name of the light to edit (alternative to light_id)"}}},
            {"type",             {{"type", "string"},  {"enum", json::array({"directional", "point", "spot"})}, {"description", "New light type"}}},
            {"position",         {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New world position [x, y, z] for the light's node"}}},
            {"color",            {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "New linear RGB color [r, g, b]"}}},
            {"intensity",        {{"type", "number"},  {"description", "New light intensity"}}},
            {"range",            {{"type", "number"},  {"description", "New light range / far distance"}}},
            {"cast_shadow",      {{"type", "boolean"}, {"description", "Whether the light casts shadows"}}},
            {"inner_spot_angle", {{"type", "number"}, {"description", "Spot inner cone angle in radians"}}},
            {"outer_spot_angle", {{"type", "number"}, {"description", "Spot outer cone angle in radians"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_camera",         "Edit an existing camera in place (by camera_id or camera_name). 'exposure' scales scene content brightness in the standard shader; overlay meshes (transform gizmo, hotbar) deliberately ignore it. Only the provided fields change.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",   {{"type", "string"},  {"description", "Name of the scene"}}},
            {"camera_id",    {{"type", "integer"}, {"description", "ID of the camera to edit (from get_scene_cameras)"}}},
            {"camera_name",  {{"type", "string"},  {"description", "Name of the camera to edit (alternative to camera_id)"}}},
            {"exposure",     {{"type", "number"},  {"description", "New exposure value (1.0 = neutral)"}}},
            {"shadow_range", {{"type", "number"},  {"description", "New shadow range / far distance"}}},
            {"fov_y",        {{"type", "number"},  {"description", "New vertical field of view in radians (perspective_vertical cameras)"}}},
            {"z_near",       {{"type", "number"},  {"description", "New near clip plane distance"}}},
            {"z_far",        {{"type", "number"},  {"description", "New far clip plane distance (default projection is 64 m - big scenes clip without raising this)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"toggle_physics",     "Toggle dynamic physics simulation, or set it explicitly with 'enabled'", {
        {"type", "object"},
        {"properties", {
            {"enabled", {{"type", "boolean"}, {"description", "Explicit state; omit to toggle"}}}
        }}
    }});
    m_tool_infos.push_back({"add_node_attachment", "Add a new attachment to a scene node (undoable, executes on the next frame). 'type' is a catalog key: camera, light, mesh, rigid_body, joint, layout, layout_item, grid, frame_controller. All kinds except joint are single-instance (refused if the node already has one). layout_item requires the node's parent to have a layout. Verify with get_node_details.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Target node ID"}}},
            {"node_name",  {{"type", "string"},  {"description", "Target node name (alternative to node_id)"}}},
            {"type",       {{"type", "string"},  {"enum", json::array({"camera", "light", "mesh", "rigid_body", "joint", "layout", "layout_item", "grid", "frame_controller"})}, {"description", "Attachment catalog key"}}}
        }},
        {"required", json::array({"scene_name", "type"})}
    }});
    m_tool_infos.push_back({"remove_node_attachment", "Remove an attachment from a scene node (undoable pure detach, executes on the next frame). Identify the attachment by attachment_id, or by 'type' = its attachment type name as reported by get_node_details (e.g. Camera, Light, Mesh, Node_physics, Grid, Layout), case-insensitive. Any attachment is removable, including ones not in the add catalog.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "Target node ID"}}},
            {"node_name",     {{"type", "string"},  {"description", "Target node name (alternative to node_id)"}}},
            {"attachment_id", {{"type", "integer"}, {"description", "ID of the attachment to remove (from get_node_details)"}}},
            {"type",          {{"type", "string"},  {"description", "Attachment type name to remove (alternative to attachment_id)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"reparent_node",     "Set a node's parent (by node IDs)",                    {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",       {{"type", "integer"}, {"description", "ID of the node to reparent"}}},
            {"parent_node_id",{{"type", "integer"}, {"description", "ID of the new parent node (0 for scene root)"}}}
        }},
        {"required", json::array({"scene_name", "node_id"})}
    }});
    m_tool_infos.push_back({"clipboard_copy_nodes", "Copy scene nodes (by ID) into the editor clipboard. Same semantics as the interactive Copy: the clipboard holds ownerless clones of the node subtrees, so the content survives closing the source scene and can be pasted into another scene with clipboard_paste.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the source scene"}}},
            {"node_ids",   {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "IDs of the nodes to copy (each with its subtree)"}}}
        }},
        {"required", json::array({"scene_name", "node_ids"})}
    }});
    m_tool_infos.push_back({"clipboard_paste", "Paste the editor clipboard contents under a node of a scene (undoable, like Ctrl+V). Materials carried by the pasted meshes keep their owning scene when it is still open; when the source scene is gone they are explicitly re-registered into the target scene (R5.2b paste-site ownership decision).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the target scene"}}},
            {"parent_node_id", {{"type", "integer"}, {"description", "ID of the node to paste under (0 or omitted for scene root)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"lock_items",         "Lock items by ID (prevents deletion/modification)",   {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to lock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"unlock_items",       "Unlock items by ID",                                  {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to unlock"}}}
        }},
        {"required", json::array({"scene_name", "ids"})}
    }});
    m_tool_infos.push_back({"add_tags",           "Add tags to items by ID",                             {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs to tag"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to add"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    m_tool_infos.push_back({"remove_tags",        "Remove tags from items by ID",                        {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Item IDs"}}},
            {"tags",       {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Tags to remove"}}}
        }},
        {"required", json::array({"scene_name", "ids", "tags"})}
    }});
    json texture_slot_schema = {
        {"type", "object"},
        {"properties", {
            {"texture",   {{"description", "Texture: string (name in content library), integer (texture id), or null to clear. Omit to keep current."}}},
            {"tex_coord", {{"type", "integer"}, {"minimum", 0},  {"description", "UV channel index (e.g. 0 for TEXCOORD_0)"}}},
            {"rotation",  {{"type", "number"}, {"description", "UV rotation in radians"}}},
            {"offset",    {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV offset [u, v]"}}},
            {"scale",     {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 2}, {"maxItems", 2}, {"description", "UV scale [u, v]"}}}
        }}
    };
    // Material fields shared by edit_material and create_material (the
    // handlers share apply_material_fields in mcp_server_material.cpp).
    const json material_field_properties = {
        {"base_color",                 {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB base color [r, g, b]"}}},
        {"opacity",                    {{"type", "number"},  {"description", "Opacity in [0, 1]"}}},
        {"roughness",                  {{"description", "Roughness; either [x, y] for anisotropic or a single number applied to both."}}},
        {"metallic",                   {{"type", "number"},  {"description", "Metallic factor in [0, 1]"}}},
        {"reflectance",                {{"type", "number"},  {"description", "Dielectric reflectance in [0, 1]"}}},
        {"emissive",                   {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Linear RGB emissive [r, g, b]"}}},
        {"ior",                        {{"type", "number"},  {"description", "Index of refraction in [1, 3] (KHR_materials_ior; default 1.5)"}}},
        {"transmission",               {{"type", "number"},  {"description", "Transmission factor in [0, 1] (KHR_materials_transmission); > 0 makes the GPU ray tracer refract through the surface"}}},
        {"normal_texture_scale",       {{"type", "number"},  {"description", "Normal map scale"}}},
        {"occlusion_texture_strength", {{"type", "number"},  {"description", "Occlusion map strength"}}},
        {"bxdf_model",                 {{"type", "string"},  {"enum", json::array({"unlit", "isotropic_brdf", "anisotropic_brdf", "anisotropic_slope", "anisotropic_engine_ready"})}, {"description", "Selects which BxDF the standard shader applies"}}},
        {"blending_mode",              {{"type", "string"},  {"enum", json::array({"opaque", "alpha_blend", "multiply", "add", "subtract", "screen_door", "alpha_test"})}, {"description", "Framebuffer blending: alpha_blend/multiply/add/subtract route through the translucent pass (raster translucency needs this - opacity alone renders opaque); screen_door/alpha_test discard in the opaque pass"}}},
        {"alpha_cutoff",               {{"type", "number"},  {"description", "Alpha cutoff in [0, 1] for blending_mode alpha_test (default 0.5)"}}},
        {"use_circular_brushed_metal", {{"type", "boolean"}, {"description", "Enable circular brushed metal shading variant"}}},
        {"use_aniso_control",          {{"type", "boolean"}, {"description", "Enable anisotropic shading control"}}},
        {"texture_samplers",           {
            {"type", "object"},
            {"description", "Per-slot texture assignments. Textures must come from the scene's content library (use get_scene_textures to list). Each slot holds a single texture reference, so assigning or clearing a texture also replaces/clears any Graph Texture binding on that slot."},
            {"properties", {
                {"base_color",         texture_slot_schema},
                {"metallic_roughness", texture_slot_schema},
                {"normal",             texture_slot_schema},
                {"occlusion",          texture_slot_schema},
                {"emissive",           texture_slot_schema}
            }}
        }}
    };
    {
        json properties = material_field_properties;
        properties["scene_name"]    = {{"type", "string"},  {"description", "Name of the scene (name path; not needed with material_id)"}};
        properties["material_name"] = {{"type", "string"},  {"description", "Name of the material to edit"}};
        properties["material_id"]   = {{"type", "integer"}, {"description", "Material by unique item id; reaches any scene's materials and the asset manager's loaded container materials (takes precedence over the name path)"}};
        m_tool_infos.push_back({"edit_material",      "Edit material properties including texture assignments (undoable). Only fields supplied are changed.", {
            {"type", "object"},
            {"properties", properties},
            {"required", json::array({"scene_name", "material_name"})}
        }});
    }
    {
        json properties = material_field_properties;
        properties["scene_name"] = {{"type", "string"}, {"description", "Scene whose content library receives the material"}};
        properties["name"]       = {{"type", "string"}, {"description", "Name for the new material (must not already exist in the scene; the error returns existing_id when it does)"}};
        m_tool_infos.push_back({"create_material", "Create a new material in a scene's content library and return its id. Accepts the same fields as edit_material; omitted fields keep the engine defaults (white, roughness 0.5, non-metal, opaque isotropic BRDF). Not undoable (like copy_library_item).", {
            {"type", "object"},
            {"properties", properties},
            {"required", json::array({"scene_name", "name"})}
        }});
    }

    m_tool_infos.push_back({"copy_library_item", "Copy a content-library item from one scene's library to another's. Copies never alias: the copy is a fresh item owned by the target scene (brushes share their expensive payload by reference). Name collisions in the target get a ' (N)' suffix. Textures and graph assets cannot be copied; a copied material keeps its texture references into the source scene's textures (they render, but do not export with the target scene). Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"item_name",    {{"type", "string"}, {"description", "Name of the item to copy (must be unique within its folder)"}}},
            {"item_type",    {{"type", "string"}, {"enum", json::array({"material", "brush", "physics_material", "collision_filter", "physics_joint"})}, {"description", "Item category; default material"}}},
            {"source_scene", {{"type", "string"}, {"description", "Scene whose library holds the item"}}},
            {"target_scene", {{"type", "string"}, {"description", "Scene whose library receives the copy"}}}
        }},
        {"required", json::array({"item_name", "source_scene", "target_scene"})}
    }});

    m_tool_infos.push_back({"get_scene_settings", "Get a scene's per-scene state: ambient light color, physics enable, and the per-scene setting overrides (#239; null when every field uses the editor-global default, else the Scene_settings object: sky, grid, physics, shadow_frustum_fit, camera_controls, clear_color, post_processing).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"set_scene_settings", "Set a scene's per-scene state: ambient light color and/or the per-scene setting overrides (#239). By default 'settings' REPLACES the whole Scene_settings object (omitted fields return to the editor-global default; {} clears every override); with merge: true the given fields deep-merge (RFC 7386) over the current settings instead - omitted fields keep their values, null deletes an override. These persist in the ERHE_scene extension when the scene is saved.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"}, {"description", "Name of the scene"}}},
            {"ambient_light", {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 4}, {"description", "Ambient light color [r, g, b] or [r, g, b, a]"}}},
            {"settings",      {{"type", "object"}, {"description", "Scene_settings object (same shape get_scene_settings returns): optional sky, grid, physics, shadow_frustum_fit, camera_controls, clear_color ([r,g,b,a]), post_processing (bool)"}}},
            {"merge",         {{"type", "boolean"}, {"description", "Deep-merge 'settings' over the current settings instead of replacing them (default false)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"save_scene",         "Save a scene as a single erhe-authored glTF file carrying FULL editor state (render content plus the ERHE_* extensions: scene settings, physics, layouts, brushes, node graphs, collections/tags; ERHE_scene in extensionsUsed marks the file), without a file dialog. Without 'path' the scene saves back to its own source file when it was opened/loaded from one, else to res/editor/scenes/<scene name>.glb. When the written file is a loaded prefab source, the prefab is reloaded so every instance in every scene reflects the edit (this subsumed the former save_prefab tool). This is the scene persistence path; export_gltf without editor_state is the plain interchange export.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Name of the scene"}}},
            {"path",          {{"type", "string"},  {"description", "Destination file path (default: the scene's source file, else res/editor/scenes/<scene name>.glb); .glb is appended when the extension is neither .glb nor .gltf (.gltf selects the text form)"}}},
            {"set_as_source", {{"type", "boolean"}, {"description", "Save As semantics: bind the scene (and its container record) to this path, so further saves write back to it. Default false: an explicit-path save is an export that leaves the scene's source binding untouched."}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"load_scene",         "Load a saved scene, without a file dialog: an erhe-authored glTF file (saved by save_scene) opens as a full scene with its saved editor state (fresh content library, browser + viewport windows; not undoable); a foreign glTF opens as a new scene via the same path as open_scene. The load is queued and completes on a following frame; discover the scene via list_scenes.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .glb/.gltf scene file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"open_scene",         "Open a FOREIGN glTF file as a new scene (same as the Asset Browser's Open context menu entry): creates a scene root + content library + browser window + a new viewport window showing the scene, and imports the file, all as a single undoable operation. Existing viewport windows are not modified. The open is queued and completes on a following frame; discover the scene via list_scenes (named after the file name). For an erhe-authored scene file, prefer load_scene, which also applies the saved editor state.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"close_scene",        "Close a scene: destroys its viewport and browser windows and unregisters it from the editor (same as the Scene row's Close context menu entry). The close is queued and completes on a following frame.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene to close"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"create_scene",       "Create a new, empty scene with its own camera, viewport and content library (same as the Create > Scene menu command). The new scene's content library is populated with the standard brushes shared from the default scene. The creation is queued and completes on a following frame; discover the new scene name (\"Scene N\") via list_scenes.", schema_no_args()});
    m_tool_infos.push_back({"export_gltf",        "Export a scene to a glTF file, without a file dialog. By default a plain interchange export; with editor_state=true the file additionally carries the editor-domain ERHE_* extensions (ERHE_scene settings, ERHE_physics, ERHE_layout, ERHE_brushes, ERHE_node_graphs, ERHE_collections) for full scene persistence, and baked graph-mesh products are excluded (rebuilt on load).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",   {{"type", "string"},  {"description", "Name of the scene"}}},
            {"path",         {{"type", "string"},  {"description", "Destination file path"}}},
            {"binary",       {{"type", "boolean"}, {"description", "Write binary .glb instead of text .gltf (default true)"}}},
            {"editor_state", {{"type", "boolean"}, {"description", "Include editor-domain ERHE_* extensions for full scene persistence (default false = plain interchange export)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"import_gltf",        "Import a glTF file into an existing scene. With materials_as_references (R7 import-as-reference) the parsed materials are acquired from the source container through the asset manager and listed as reference entries instead of copied definitions.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",              {{"type", "string"},  {"description", "Name of the destination scene"}}},
            {"path",                    {{"type", "string"},  {"description", "Source .gltf/.glb file path"}}},
            {"materials_as_references", {{"type", "boolean"}, {"description", "Acquire materials from the source container and list them as reference entries (default false: import-as-copy)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"scan_gltf",          "Scan a glTF file without loading it: per-category object names with their glTF 2.1 unique IDs (uid, when declared), extensions used, and scan errors", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    const json asset_key_properties = {
        {"scope", {{"type", "string"}, {"enum", json::array({"builtin", "scene_local", "file"})}, {"description", "Asset scope: builtin (manager-registered procedural item, e.g. palette brushes), scene_local (by-name lookup against the registry and open scenes), file (asset defined in a glTF container file)"}}},
        {"type",  {{"type", "string"}, {"enum", json::array({"brush", "material", "animation", "mesh"})}, {"description", "Managed asset type"}}},
        {"path",  {{"type", "string"}, {"description", "Container file path (file scope only)"}}},
        {"uid",   {{"type", "string"}, {"description", "glTF 2.1 unique ID of the asset within the container (preferred identity; file scope)"}}},
        {"name",  {{"type", "string"}, {"description", "Asset name (identity fallback; must be unique within the container for file scope)"}}}
    };
    json acquire_asset_properties = asset_key_properties;
    acquire_asset_properties["hold_name"] = {{"type", "string"}, {"description", "Name of the debug hold that keeps the asset acquired"}};
    m_tool_infos.push_back({"query_asset_manager", "Inspect the asset manager (Phase R1): registered assets (builtins, loaded container assets, scene-local userships) with their declared users, container records - loaded container files with per-type asset counts and identifiability errors, plus every open scene's identity record (R5.3: open_as_scene true; session true until the scene's first save binds it to a path, save-as re-homes it) - and the named debug holds.", schema_no_args()});
    m_tool_infos.push_back({"acquire_asset",       "Acquire an asset through the asset manager and keep it held under a named debug hold (a declared user). Loads the container on first request (file scope); repeated acquires of the same key return the same object (compare item_id). Re-using a hold_name re-targets that hold.", {
        {"type", "object"},
        {"properties", acquire_asset_properties},
        {"required", json::array({"hold_name", "scope", "type"})}
    }});
    m_tool_infos.push_back({"release_asset",       "Release a named debug hold created by acquire_asset, dropping its usership of the held asset.", {
        {"type", "object"},
        {"properties", {
            {"hold_name", {{"type", "string"}, {"description", "Name of the debug hold to release"}}}
        }},
        {"required", json::array({"hold_name"})}
    }});
    m_tool_infos.push_back({"unload_asset",        "Request unload of a file-scope asset's container. Refused while any asset of the container has users (the refusal names them) or the container is dirty (unsaved asset edits; pass 'discard': true to drop them). A successful unload verifies exclusivity: managed assets still alive afterwards are logged as undeclared users.", {
        {"type", "object"},
        {"properties", [&asset_key_properties]() {
            json properties = asset_key_properties;
            properties["discard"] = {{"type", "boolean"}, {"description", "Unload a dirty container anyway, dropping its unsaved asset edits"}};
            return properties;
        }()},
        {"required", json::array({"scope", "type", "path"})}
    }});
    m_tool_infos.push_back({"save_container",      "Write a loaded asset container back to its file. A container open as a scene delegates to the scene save (clears the record's dirty flag); an authored material container (created by make_asset_external) is rewritten from its live materials (R7 asset-file save); a container parsed from an arbitrary glTF stays read-only (its file may hold unmanaged content) - discard-unload drops its edits instead.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Path of the loaded container to save"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"load_asset_file",     "Load a glTF file as an asset container through the asset manager (one parse per container; a path open as a scene returns that scene's record). Returns the record id, per-type asset counts and identifiability errors.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Container .gltf/.glb file path"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"reference_asset_into_scene", "R7: acquire a material from a container file and list it in a scene's content library as a REFERENCE entry carrying its file-scope asset key (undoable attach). The scene save then writes an ERHE_asset_reference proxy instead of the material data. Accepted only when the defining container is path-bound (plan resolution 11).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Destination scene"}}},
            {"path",       {{"type", "string"}, {"description", "Container file path holding the material definition"}}},
            {"uid",        {{"type", "string"}, {"description", "glTF 2.1 unique ID of the material (preferred identity)"}}},
            {"name",       {{"type", "string"}, {"description", "Material name (identity fallback; must be unique in the container)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"make_asset_external", "R7 MAKE EXTERNAL: move a scene-defined material into a fresh asset container file. The file is written (export stamps the uid), the manager re-homes THE live object (meshes keep pointing at it), and the scene's library entry flips definition -> reference carrying the file key - the next scene save writes an R6 proxy. Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Scene defining the material"}}},
            {"material_id",   {{"type", "integer"}, {"description", "Material by unique item id (takes precedence over material_name)"}}},
            {"material_name", {{"type", "string"},  {"description", "Material name in the scene's library"}}},
            {"path",          {{"type", "string"},  {"description", "Container file path to create (.glb or .gltf; must not already be loaded or open as a scene)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"make_asset_internal", "R7 MAKE INTERNAL: copy a shared (referenced) material's data into a scene-owned definition and de-link - the scene's mesh primitives swap to the copy, the reference entry becomes an owning definition entry, and edits stop reaching other users of the shared object. The copy is a new asset (no uid until its first export). Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",    {{"type", "string"},  {"description", "Scene holding the reference entry"}}},
            {"material_id",   {{"type", "integer"}, {"description", "Shared material by unique item id (takes precedence over material_name)"}}},
            {"material_name", {{"type", "string"},  {"description", "Shared material name in the scene's library"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"set_tool_asset",      "Set or clear the asset a tool holds as an Asset_reference (Phase R3): the Brush Tool's active brush or the Material Paint Tool's material. The tool becomes a declared user of the asset (visible in query_asset_manager). Omit 'name' to clear.", {
        {"type", "object"},
        {"properties", {
            {"tool",       {{"type", "string"}, {"enum", json::array({"brush", "material_paint"})}, {"description", "Which tool's asset to set"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene whose content library is searched (required unless clearing)"}}},
            {"name",       {{"type", "string"}, {"description", "Brush / material name in that scene's content library; omit or empty to clear the tool's asset"}}}
        }},
        {"required", json::array({"tool"})}
    }});
    m_tool_infos.push_back({"set_inventory_slot",  "Adopt a manager-known brush / material into an Inventory grid slot exactly as a drag-drop would (Asset_reference::adopt; the slot becomes a declared user and its autosaved v4 key is the returned 'key'), or clear the slot's asset references with 'clear': true. Look up item ids via get_scene_materials / get_scene_brushes / query_asset_manager.", {
        {"type", "object"},
        {"properties", {
            {"slot_index", {{"type", "integer"}, {"description", "Zero-based Inventory grid slot index"}}},
            {"item_id",    {{"type", "integer"}, {"description", "Item id of a manager-known brush or material (required unless clearing)"}}},
            {"clear",      {{"type", "boolean"}, {"description", "Clear the slot's brush / material references instead of adopting"}}}
        }},
        {"required", json::array({"slot_index"})}
    }});
    m_tool_infos.push_back({"instantiate_prefab", "Instantiate a glTF file as a prefab into a scene: the file is parsed once (cached app-wide) and inserted as a clone that stays a reference to the source file. Instances share GPU buffers; insertion is undoable.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the destination scene"}}},
            {"path",       {{"type", "string"}, {"description", "Source .gltf/.glb file path"}}},
            {"position",   {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position [x, y, z] of the instance root (default origin)"}}}
        }},
        {"required", json::array({"scene_name", "path"})}
    }});
    m_tool_infos.push_back({"reload_prefab",      "Re-parse a loaded prefab from its source glTF file and refresh every instance in every scene (prefabs whose templates reference it are rebuilt too, in dependency order). Carrier node transforms are preserved. Not undoable.", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Source .gltf/.glb file path of a loaded prefab"}}}
        }},
        {"required", json::array({"path"})}
    }});
    m_tool_infos.push_back({"get_prefabs",        "List the glTF prefabs currently loaded in the app-wide prefab library (source path, name, content counts)", schema_no_args()});
    m_tool_infos.push_back({"capture_screenshot",  "Capture the current rendered frame to a PNG file and return its path. Currently supported only in the headless Vulkan configuration (emulated swapchain).", {
        {"type", "object"},
        {"properties", {
            {"path", {{"type", "string"}, {"description", "Output PNG path (default logs/mcp_screenshot.png)"}}}
        }}
    }});
    m_tool_infos.push_back({"set_ray_trace", "Enable/disable the GPU ray tracing renderer (issue #233: material-aware ray query compute shader with lights, traced shadows and glass, rendered into the texture shown in the Ray Trace window) and optionally show that window / adjust its settings. Returns supported/enabled state and the instance count gathered on the last traced frame. Requires a Vulkan device with ray query + position fetch support.", {
        {"type", "object"},
        {"properties", {
            {"enabled",          {{"type", "boolean"}, {"description", "Enable (true) or disable (false) the renderer; omit to leave unchanged"}}},
            {"show_window",      {{"type", "boolean"}, {"description", "Also show the Ray Trace window so the output is visible in screenshots (default false)"}}},
            {"save_path",        {{"type", "string"},  {"description", "When set, read the ray traced output texture back and write it to this PNG path (sRGB-encoded for viewing)"}}},
            {"downscale",        {{"type", "number"},  {"description", "Output downscale factor, [1.0, 8.0]: 1 = one ray per viewport pixel, 2 = each traced pixel covers 2x2 viewport pixels; integer values display with nearest magnification, fractional with linear; omit to leave unchanged"}}},
            {"max_rays",         {{"type", "integer"}, {"description", "Per-pixel traced-ray budget for the Whitted branching loop, [1, 1024]; omit to leave unchanged"}}},
            {"max_bounces",      {{"type", "integer"}, {"description", "Max transmissive interactions along one branch, [0, 12]; omit to leave unchanged"}}}
        }}
    }});
    m_tool_infos.push_back({"wake_physics_bodies", "Activate all dynamic rigid bodies in a scene (bodies enter the world deactivated)", schema_scene_name()});
    m_tool_infos.push_back({"apply_physics_force", "Apply an external force / torque / impulse to a dynamic rigid body (world space, activates the body). Forces and torques accumulate for the next fixed step only (re-apply for a sustained push); impulses change velocity immediately. 'point' (world position) makes force / impulse act at that point instead of the center of mass.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",  {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"force",      {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Force [x, y, z] in newtons"}}},
            {"torque",     {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Torque [x, y, z] in newton meters"}}},
            {"impulse",    {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Impulse [x, y, z] in newton seconds"}}},
            {"point",      {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "World position where force / impulse is applied (default: center of mass)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    m_tool_infos.push_back({"get_physics_items", "List the shared physics content-library items (physics materials, collision filters, joint settings) with full properties", schema_scene_name()});

    const json node_ref_properties = {
        {"node_id",   {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
        {"node_name", {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
    };
    const json shape_properties = {
        {"shape",         {{"type", "string"}, {"enum", json::array({"auto", "box", "sphere", "capsule", "tapered_capsule", "cylinder", "tapered_cylinder", "convex_hull", "mesh"})}, {"description", "Collision shape; auto (default) = convex hull from the node's mesh, unit box when the node has no mesh. mesh shapes are static/kinematic only."}}},
        {"half_extents",  {{"type", "array"},  {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Box / cylinder half extents [x, y, z] (default [0.5, 0.5, 0.5])"}}},
        {"radius",        {{"type", "number"}, {"description", "Sphere / capsule radius (default 0.5)"}}},
        {"bottom_radius", {{"type", "number"}, {"description", "Tapered capsule / cylinder bottom radius (default 0.5)"}}},
        {"top_radius",    {{"type", "number"}, {"description", "Tapered capsule / cylinder top radius (default 0.5)"}}},
        {"length",        {{"type", "number"}, {"description", "Capsule / tapered shape axial length (default 1.0)"}}},
        {"axis",          {{"type", "string"}, {"enum", json::array({"x", "y", "z"})}, {"description", "Shape axis (default y)"}}}
    };
    const json body_properties = {
        {"motion_mode",      {{"type", "string"}, {"enum", json::array({"static", "kinematic", "kinematic_non_physical", "dynamic"})}, {"description", "Motion mode (default dynamic; kinematic = kinematic physical)"}}},
        {"mass",             {{"type", "number"}, {"description", "Mass; 0 = spec infinite-mass convention; omitted = derived from the shape"}}},
        {"friction",         {{"type", "number"}, {"description", "Scalar friction (overridden by a physics material)"}}},
        {"restitution",      {{"type", "number"}, {"description", "Scalar restitution (overridden by a physics material)"}}},
        {"linear_damping",   {{"type", "number"}, {"description", "Linear damping"}}},
        {"angular_damping",  {{"type", "number"}, {"description", "Angular damping"}}},
        {"gravity_factor",   {{"type", "number"}, {"description", "Gravity factor (default 1.0)"}}},
        {"wind_receptivity", {{"type", "number"}, {"description", "Wind drag coefficient in kg/s: the scene wind applies force = wind_receptivity * (wind_velocity - body_velocity) each fixed step; 0 (default) = unaffected"}}},
        {"is_trigger",       {{"type", "boolean"}, {"description", "Create as a sensor / trigger volume"}}},
        {"linear_velocity",  {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial linear velocity [x, y, z] (world space, applied at body creation)"}}},
        {"angular_velocity", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Initial angular velocity [x, y, z] (world space, applied at body creation)"}}},
        {"center_of_mass",   {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Center of mass offset [x, y, z]"}}},
        {"material_name",    {{"type", "string"}, {"description", "Physics material name from the content library (empty string clears)"}}},
        {"filter_name",      {{"type", "string"}, {"description", "Collision filter name from the content library (empty string clears)"}}}
    };
    json create_body_properties = {{"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}}};
    create_body_properties.update(node_ref_properties);
    create_body_properties.update(shape_properties);
    create_body_properties.update(body_properties);
    create_body_properties["wake"] = {{"type", "boolean"}, {"description", "Wake the (dynamic) body as soon as it enters the world instead of starting asleep; replaces a follow-up wake_physics_bodies pass"}};
    m_tool_infos.push_back({"create_physics_body", "Attach a new rigid body (Node_physics) to a scene node (undoable). One rigid body per node.", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"edit_physics_body", "Edit the rigid body attached to a scene node. Only fields supplied are changed; shape fields replace the collision shape (recreates the body).", {
        {"type", "object"},
        {"properties", create_body_properties},
        {"required", json::array({"scene_name"})}
    }});

    json joint_properties = {
        {"scene_name",          {{"type", "string"},  {"description", "Name of the scene"}}},
        {"connected_node_id",   {{"type", "integer"}, {"description", "Connected node ID (no connected node = constrain to the world)"}}},
        {"connected_node_name", {{"type", "string"},  {"description", "Connected node name"}}},
        {"settings_name",       {{"type", "string"},  {"description", "Physics joint settings name from the content library (empty = free six-dof joint)"}}},
        {"enable_collision",    {{"type", "boolean"}, {"description", "Keep collision enabled between the joined bodies (default false)"}}}
    };
    joint_properties.update(node_ref_properties);
    m_tool_infos.push_back({"create_physics_joint", "Attach a new joint (Node_joint) to a scene node (undoable): joins the nearest self-or-ancestor rigid body of the node to that of the connected node (or the world)", {
        {"type", "object"},
        {"properties", joint_properties},
        {"required", json::array({"scene_name"})}
    }});
    json edit_joint_properties = joint_properties;
    edit_joint_properties.update(json{
        {"joint_index",      {{"type", "integer"}, {"description", "Index among the node's joint attachments (default 0)"}}},
        {"connect_to_world", {{"type", "boolean"}, {"description", "Clear the connected node (constrain to the world)"}}},
        {"rebuild",          {{"type", "boolean"}, {"description", "Rebuild the constraint, re-capturing joint frames from current node transforms"}}}
    });
    m_tool_infos.push_back({"edit_physics_joint", "Edit a joint attached to a scene node. Only fields supplied are changed; changes rebuild the constraint.", {
        {"type", "object"},
        {"properties", edit_joint_properties},
        {"required", json::array({"scene_name"})}
    }});

    const json physics_material_value_properties = {
        {"static_friction",     {{"type", "number"}, {"description", "Static friction (default 0.6)"}}},
        {"dynamic_friction",    {{"type", "number"}, {"description", "Dynamic friction (default 0.6)"}}},
        {"restitution",         {{"type", "number"}, {"description", "Restitution (default 0.0)"}}},
        {"friction_combine",    {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Friction combine mode"}}},
        {"restitution_combine", {{"type", "string"}, {"enum", json::array({"average", "minimum", "maximum", "multiply"})}, {"description", "Restitution combine mode"}}}
    };
    json create_physics_material_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new physics material (must not already exist)"}}}
    };
    create_physics_material_properties.update(physics_material_value_properties);
    m_tool_infos.push_back({"create_physics_material", "Create a shared physics material in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_physics_material_properties = create_physics_material_properties;
    edit_physics_material_properties["name"] = {{"type", "string"}, {"description", "Name of the physics material to edit"}};
    edit_physics_material_properties["new_name"] = {{"type", "string"}, {"description", "Rename the physics material"}};
    m_tool_infos.push_back({"edit_physics_material", "Edit a shared physics material; changes re-apply to all bodies using it. Only fields supplied are changed.", {
        {"type", "object"},
        {"properties", edit_physics_material_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json collision_filter_value_properties = {
        {"collision_systems",        {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Systems the body belongs to"}}},
        {"collide_with_systems",     {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Non-empty = collide only with these systems (allowlist)"}}},
        {"not_collide_with_systems", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Used when collide_with_systems is empty: never collide with these"}}}
    };
    json create_collision_filter_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new collision filter (must not already exist)"}}}
    };
    create_collision_filter_properties.update(collision_filter_value_properties);
    m_tool_infos.push_back({"create_collision_filter", "Create a shared collision filter in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_collision_filter_properties = create_collision_filter_properties;
    edit_collision_filter_properties["name"] = {{"type", "string"}, {"description", "Name of the collision filter to edit"}};
    edit_collision_filter_properties["new_name"] = {{"type", "string"}, {"description", "Rename the collision filter"}};
    m_tool_infos.push_back({"edit_collision_filter", "Edit a shared collision filter; system lists supplied replace the existing lists and re-apply to all bodies using the filter", {
        {"type", "object"},
        {"properties", edit_collision_filter_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    const json joint_settings_value_properties = {
        {"limits", {
            {"type", "array"},
            {"description", "Per-axis limit entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"linear_axes",  {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation axes [x, y, z] this limit applies to"}}},
                    {"angular_axes", {{"type", "array"}, {"items", {{"type", "boolean"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Rotation axes [x, y, z] this limit applies to"}}},
                    {"min",          {{"type", "number"}, {"description", "Lower bound (absent = unbounded)"}}},
                    {"max",          {{"type", "number"}, {"description", "Upper bound (absent = unbounded; min == max fixes the axis)"}}},
                    {"stiffness",    {{"type", "number"}, {"description", "Soft limit spring stiffness (absent = hard limit)"}}},
                    {"damping",      {{"type", "number"}, {"description", "Soft limit damping"}}}
                }}
            }}
        }},
        {"drives", {
            {"type", "array"},
            {"description", "Drive (motor) entries"},
            {"items", {
                {"type", "object"},
                {"properties", {
                    {"type",            {{"type", "string"},  {"enum", json::array({"linear", "angular"})}}},
                    {"mode",            {{"type", "string"},  {"enum", json::array({"force", "acceleration"})}}},
                    {"axis",            {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}},
                    {"max_force",       {{"type", "number"},  {"description", "Maximum force (absent = unlimited)"}}},
                    {"position_target", {{"type", "number"}}},
                    {"velocity_target", {{"type", "number"}}},
                    {"stiffness",       {{"type", "number"},  {"description", "> 0 selects a position motor, else a velocity motor"}}},
                    {"damping",         {{"type", "number"}}}
                }}
            }}
        }}
    };
    json create_joint_settings_properties = {
        {"scene_name", {{"type", "string"}, {"description", "Name of the scene"}}},
        {"name",       {{"type", "string"}, {"description", "Name for the new joint settings (must not already exist)"}}}
    };
    create_joint_settings_properties.update(joint_settings_value_properties);
    m_tool_infos.push_back({"create_physics_joint_settings", "Create shared physics joint settings (limits + drives) in the scene's content library (undoable)", {
        {"type", "object"},
        {"properties", create_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});
    json edit_joint_settings_properties = create_joint_settings_properties;
    edit_joint_settings_properties["name"] = {{"type", "string"}, {"description", "Name of the joint settings to edit"}};
    edit_joint_settings_properties["new_name"] = {{"type", "string"}, {"description", "Rename the joint settings"}};
    m_tool_infos.push_back({"edit_physics_joint_settings", "Edit shared physics joint settings; limits / drives arrays supplied replace the existing ones and all joints using the settings are rebuilt", {
        {"type", "object"},
        {"properties", edit_joint_settings_properties},
        {"required", json::array({"scene_name", "name"})}
    }});

    // Mesh component (vertex / edge / face) selection, used by Align and Add Joint.
    const json component_mode_property = {
        {"mode", {{"type", "string"}, {"enum", json::array({"object", "vertex", "edge", "face", "bone"})}, {"description", "Selection granularity: object, vertex, edge, face, or bone (skeleton bones)"}}}
    };
    m_tool_infos.push_back({"set_mesh_component_mode", "Set the selection granularity (object / vertex / edge / face / bone). vertex/edge/face are required before select_mesh_components and Align / Add Joint. bone makes skeleton bones pickable in the viewport: a click selects the joint Node the bone stands for, through the ordinary selection.", {
        {"type", "object"},
        {"properties", component_mode_property},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"select_mesh_components", "Select sub-components (vertices / edges / faces) of a node's mesh, addressed by Geogram indices into its render geometry. extend=false (default) replaces the whole component selection first; extend=true accumulates (use it to add a second node's component for Align / Add Joint). Indices are validated against the geometry. Edges are [v0, v1] vertex-index pairs.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"mode",            {{"type", "string"},  {"enum", json::array({"object", "vertex", "edge", "face"})}, {"description", "Set the component mode before selecting (default: keep current)"}}},
            {"extend",          {{"type", "boolean"}, {"description", "Accumulate instead of replacing the whole selection (default false)"}}},
            {"vertices",        {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Vertex indices to select"}}},
            {"edges",           {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "Edges as [v0, v1] vertex-index pairs"}}},
            {"facets",          {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Facet (polygon) indices to select"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_mesh_component_selection", "Get the current mesh-component selection: mode plus each entry's node, primitive index, selected vertices / edges / facets, and whether it is live.", schema_no_args()});
    m_tool_infos.push_back({"grow_mesh_selection", "Blender Select More: grow the current mesh-component selection by one border ring, in the active mode (vertex / edge / face). No-op in object mode. Returns the resulting selection (same shape as get_mesh_component_selection).", schema_no_args()});
    m_tool_infos.push_back({"shrink_mesh_selection", "Blender Select Less: shrink the current mesh-component selection by dropping the components on its border, in the active mode (vertex / edge / face). No-op in object mode. Returns the resulting selection (same shape as get_mesh_component_selection).", schema_no_args()});
    m_tool_infos.push_back({"get_id_range_mapping", "Report the GPU ID-buffer range mapping from the most recently resolved region scan: for each drawn primitive, its id_offset, length (index count), triangle_count, base_vertex, and the owning mesh/node/primitive_index. A decoded pixel id in [id_offset, id_offset+length) selects that primitive and (id - id_offset) is its 0-based facet index. Run a box/paint select (or debug_region_select) first to populate it.", schema_no_args()});
    m_tool_infos.push_back({"debug_region_select", "Debug/test: drive a region face-select (box or paint brush) over an explicit viewport-pixel rectangle, bypassing the mouse. Forces Face component mode, requests a GPU id-buffer scan, and commits the visible faces a few frames later (poll get_mesh_component_selection afterwards). x,y,width,height are in viewport pixels; is_brush masks to a disk of brush_radius centered in the rect.", {
        {"type", "object"},
        {"properties", {
            {"x",            {{"type", "integer"}, {"description", "Rectangle left in viewport pixels"}}},
            {"y",            {{"type", "integer"}, {"description", "Rectangle top in viewport pixels"}}},
            {"width",        {{"type", "integer"}, {"description", "Rectangle width in viewport pixels"}}},
            {"height",       {{"type", "integer"}, {"description", "Rectangle height in viewport pixels"}}},
            {"is_brush",     {{"type", "boolean"}, {"description", "Mask the rectangle to a centered disk (paint brush), default false"}}},
            {"brush_radius", {{"type", "number"},  {"description", "Disk radius in pixels when is_brush is true"}}},
            {"replace",      {{"type", "boolean"}, {"description", "Clear the selection before adding (default true)"}}},
            {"subtract",     {{"type", "boolean"}, {"description", "Remove the scanned faces instead of adding (default false)"}}}
        }},
        {"required", json::array({"x", "y", "width", "height"})}
    }});
    m_tool_infos.push_back({"get_mesh_geometry_info", "Inspect a node's render geometry: element counts (vertices, edges, facets, corners) and, per domain (facet / vertex / corner), the list of attributes that are present (name, GEO type, and how many elements carry the attribute). Use this before get_mesh_attribute_values to learn what to query. Note: a flat-shaded mesh stores per-corner normals (corner_normal); a smooth mesh stores per-vertex normals.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"get_mesh_attribute_values", "Read attribute presence and values for specific mesh elements of a node's render geometry. Pick a domain (vertex / corner / facet / edge) and pass the element indices; each element reports its per-attribute {present, value}. vertex elements also report position; corner elements report their vertex + facet; facet elements report their corner + vertex lists; edge elements report endpoint vertices + adjacent facets plus edge_sharpness when present (see set_edge_sharpness). Optionally restrict to named attributes; default returns all attributes of the domain.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"domain",          {{"type", "string"},  {"enum", json::array({"vertex", "corner", "facet", "edge"})}, {"description", "Which element domain the indices address"}}},
            {"indices",         {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Element indices to read (max 4096)"}}},
            {"attributes",      {{"type", "array"},   {"items", {{"type", "string"}}}, {"description", "Optional attribute-name filter (e.g. [\"corner_normal\"]); default: all attributes in the domain"}}}
        }},
        {"required", json::array({"scene_name", "domain", "indices"})}
    }});
    m_tool_infos.push_back({"clear_mesh_component_selection", "Clear the entire mesh-component selection", schema_no_args()});
    m_tool_infos.push_back({"set_edge_sharpness", "Set (or clear) the semi-sharp crease sharpness (edge_sharpness attribute) of mesh edges; Catmull-Clark subdivision then applies the sharp rules for sharpness levels (fractional part blends, decays by ~1 per level, \"infinity\" never smooths). Targets explicit [v0, v1] edge pairs, or the current edge component selection on the geometry when edges is omitted. Undoable; queued (barrier: any query). Read values back with get_mesh_attribute_values domain=edge.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",      {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",         {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",       {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}},
            {"primitive_index", {{"type", "integer"}, {"description", "Mesh primitive index (default 0)"}}},
            {"edges",           {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"minItems", 2}, {"maxItems", 2}}}, {"description", "Edges as [v0, v1] vertex-index pairs; omit to use the current edge component selection"}}},
            {"sharpness",       {{"description", "Sharpness value >= 0 (number), or the string \"infinity\" for an infinitely sharp crease"}}},
            {"clear",           {{"type", "boolean"}, {"description", "Remove the sharpness values (back to smooth) instead of setting one (default false)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"align_components", "Align the two selected mesh components (of the active vertex/edge/face mode) on two distinct nodes: colocate vertices, align edges, or glue faces. apply_scale also matches scale (edge/face only). Requires exactly two components selected on two distinct nodes. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"apply_scale", {{"type", "boolean"}, {"description", "Also apply uniform scale to match the components (edge/face modes; default false)"}}}
        }}
    }});
    m_tool_infos.push_back({"add_joint", "Align the two selected mesh components, then create a physics joint between the two nodes' rigid bodies (ball for vertex, hinge for edge/face). Both nodes must already have a rigid body. Searches the joint's free rotational DOF for a non-intersecting placement; fails if none is found. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the initial-orientation search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"flip_joint", "Flip the hinge joint that the selected node is a rigid-body party of: reorients that body by a 180-degree edge-endpoints-swapped flip plus a non-intersecting roll, re-pins its joint frame, and rebuilds the constraint. Select a rigid-body party of a hinge joint first. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"avoidance", {{"type", "string"}, {"enum", json::array({"joint_pair", "whole_world"})}, {"description", "What the roll search avoids intersecting: just the joined pair (default) or every body in the physics world"}}}
        }}
    }});
    m_tool_infos.push_back({"get_physics_state", "Get the live physics motion state of a node's rigid body: motion mode, active flag, world position, and linear / angular velocity (with magnitudes). Poll across frames to observe how a body reacts after an operation.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Node ID (preferred over node_name when both given)"}}},
            {"node_name",  {{"type", "string"},  {"description", "Node name (used when node_id is absent)"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    // Geogram mesh operations - act on the object selection (set via select_items).
    m_tool_infos.push_back({"remesh", "Geogram isotropic / anisotropic remesh of the selected mesh node(s) to a target vertex count (queued, runs over subsequent frames - poll get_async_status). anisotropy=0 (default) is isotropic; anisotropy>0 (e.g. 0.04) is anisotropic. Acts on the current object selection, or on explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
            {"node_ids",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Single explicit target node id"}}},
            {"node_name",  {{"type", "string"},  {"description", "Single explicit target node name"}}},
            {"target_vertex_count",   {{"type", "integer"}, {"description", "Target vertex count (Geogram nb_points, default 2000)"}}},
            {"anisotropy",            {{"type", "number"},  {"description", "0 = isotropic (default); >0 = anisotropic strength (e.g. 0.04)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"decimate", "Geogram vertex-clustering decimation of the selected mesh node(s) (queued). bins is the clustering grid resolution (higher = more detail kept). Acts on the current object selection, or on explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
            {"node_ids",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Single explicit target node id"}}},
            {"node_name",  {{"type", "string"},  {"description", "Single explicit target node name"}}},
            {"bins",                  {{"type", "integer"}, {"description", "Vertex-clustering grid resolution (default 50)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"smooth", "Geogram Laplacian smoothing of the selected mesh node(s) (queued). Vertex count is preserved. Acts on the current object selection, or on explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
            {"node_ids",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Single explicit target node id"}}},
            {"node_name",  {{"type", "string"},  {"description", "Single explicit target node name"}}},
            {"iterations",            {{"type", "integer"}, {"description", "Smoothing iterations (default 5)"}}},
            {"strength",              {{"type", "number"},  {"description", "Smoothing strength [0,1] (default 0.5)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Regenerate smooth normals and texture coordinates (default true)"}}}
        }}
    }});
    m_tool_infos.push_back({"chamfer", "Conway chamfer of the selected mesh node(s) (queued): shrink each facet toward its centroid by bevel_ratio and replace every edge with a hexagonal face. When a FACE-mode mesh-component selection is active, only the selected facets are chamfered and the result stays welded watertight to the rest of the mesh (selected facets inset, interior shared edges become hexagons, boundary edges become bevel quads); otherwise the whole mesh is chamfered. Acts on the current object or face-component selection, or on explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
            {"node_ids",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
            {"node_id",    {{"type", "integer"}, {"description", "Single explicit target node id"}}},
            {"node_name",  {{"type", "string"},  {"description", "Single explicit target node name"}}},
            {"bevel_ratio", {{"type", "number"}, {"description", "How much each face shrinks toward its centroid, [0,1] (default 0.25)"}}}
        }}
    }});
    m_tool_infos.push_back({"csg", "CSG boolean between mesh nodes (queued, undoable as ONE operation): target <operation> tool(s), composed in WORLD space. The result geometry REPLACES the target node's mesh primitives in place - the target keeps its node id, name, transform, children, material and physics attachment (collision shape is rebuilt as a convex hull of the result). The tool nodes are then REMOVED from the scene (their children, if any, reparent to the tool's parent - pass leaf mesh nodes as tools). Multiple tools (tool_node_ids) merge into ONE tool solid and apply in a single pass - STRONGLY preferred over sequential calls (each pass re-triangulates the whole target, and stacked passes leave sliver-triangle shading artifacts). Inputs should be closed watertight manifolds (capped cones etc.); an empty result leaves the scene unchanged and logs a message. Note: on a pooled brush instance the edited target silently goes private (same rule as the other geometry ops). Executes on the target's scene; poll get_async_status before reading the result.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",     {{"type", "string"},  {"description", "Name of the scene (required)"}}},
            {"operation",      {{"type", "string"},  {"description", "Boolean operation: union, intersection or difference (target minus tools)"}}},
            {"node_id",        {{"type", "integer"}, {"description", "Target (A) node id (takes precedence over node_name)"}}},
            {"node_name",      {{"type", "string"},  {"description", "Target (A) node name"}}},
            {"tool_node_id",   {{"type", "integer"}, {"description", "Tool (B) node id (takes precedence over tool_node_name)"}}},
            {"tool_node_name", {{"type", "string"},  {"description", "Tool (B) node name"}}},
            {"tool_node_ids",  {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Multiple tool node ids - merged into one tool solid, applied in one pass (preferred for e.g. a row of carve boxes)"}}}
        }},
        {"required", json::array({"scene_name", "operation"})}
    }});
    m_tool_infos.push_back({"lattice_deform", "Free-form deformation (FFD) of mesh node(s) through a control point lattice (queued, undoable). The cage is an axis-aligned box in the mesh's LOCAL space, auto-fitted to the geometry's bounds unless cage_min/cage_max are given. offsets lists control point displacements [i, j, k, dx, dy, dz]; unlisted points stay at rest. divisions [x,y,z] cells means (x+1)*(y+1)*(z+1) control points, index 0..divisions per axis. bezier interpolation (default) gives smooth global FFD - THE tool for billowing sails, bent planks, tapered curves; trilinear is local per-cell. The source mesh needs interior vertices to bend (a 4-vertex quad cannot billow - use a box with steps, e.g. size [w,h,0.02] steps [8,8,1]). Topology is unchanged. Acts on the current object selection, or on explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored).", {
        {"type", "object"},
        {"properties", {
            {"scene_name",  {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
            {"node_ids",    {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
            {"node_id",     {{"type", "integer"}, {"description", "Single explicit target node id"}}},
            {"node_name",   {{"type", "string"},  {"description", "Single explicit target node name"}}},
            {"divisions",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Lattice cell count per axis (default [2,2,2] = 27 control points)"}}},
            {"offsets",     {{"type", "array"},   {"items", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 6}, {"maxItems", 6}}}, {"description", "Control point displacements [i, j, k, dx, dy, dz] in cage (local) space; required"}}},
            {"cage_min",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Cage box min corner in mesh-local space (give with cage_max; omit both for auto fit)"}}},
            {"cage_max",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Cage box max corner in mesh-local space"}}},
            {"interpolation", {{"type", "string"}, {"description", "bezier (default; smooth global FFD) or trilinear (local per-cell)"}}},
            {"regenerate_attributes", {{"type", "boolean"}, {"description", "Recompute smooth normals from the deformed positions (default true)"}}}
        }},
        {"required", json::array({"offsets"})}
    }});
    json geometry_target_properties = {
        {"scene_name", {{"type", "string"},  {"description", "Scene for node targets (required with node_ids / node_id / node_name)"}}},
        {"node_ids",   {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Explicit target node ids; the previous selection is restored after the call"}}},
        {"node_id",    {{"type", "integer"}, {"description", "Single explicit target node id"}}},
        {"node_name",  {{"type", "string"},  {"description", "Single explicit target node name"}}}
    };
    m_tool_infos.push_back({"catmull_clark", "Apply one level of Catmull-Clark subdivision to the selected mesh node(s), or to explicit node targets (node_ids / node_id / node_name + scene_name; the previous selection is restored). Honors the per-edge edge_sharpness crease attribute (see set_edge_sharpness): sharp rules for sharpness levels, fractional blend, child edges carry decremented sharpness. Queued and undoable.", {
        {"type", "object"},
        {"properties", geometry_target_properties}
    }});
    m_tool_infos.push_back({"merge_faces", "Merge (dissolve) the selected facets of the mesh node(s) into one polygon per edge-connected group: facets connected through a shared EDGE (not merely a shared vertex) become a single polygon spanning their boundary loop, dropping the now-interior edges and vertices. Requires an active FACE-mode mesh-component selection (set_mesh_component_mode face + select_mesh_components). A group whose boundary is not a single simple loop (encloses a hole / pinches) is left unchanged. Queued; the rest of the mesh stays watertight. Node targets (node_ids / node_id / node_name + scene_name) override the object selection.", {
        {"type", "object"},
        {"properties", geometry_target_properties}
    }});
    m_tool_infos.push_back({"generate_texture_coordinates", "Generate texture coordinates for the selected mesh node(s) via Geogram mesh_make_atlas (queued). Writes UVs into the given corner texcoord channel. Acts on the current object selection.", {
        {"type", "object"},
        {"properties", {
            {"texcoord_slot",   {{"type", "integer"}, {"description", "Target corner texcoord channel 0, 1 or 2 (default 0; 2 is the lightmap channel)"}}},
            {"hard_angles_deg", {{"type", "number"},  {"description", "Hard-angle threshold in degrees for chart seams (default 45)"}}},
            {"parameterizer",   {{"type", "integer"}, {"description", "Atlas_parameterizer enum index (default 3 = ABF)"}}},
            {"packer",          {{"type", "integer"}, {"description", "Atlas_packer enum index (default 2 = XAtlas)"}}}
        }}
    }});

    m_tool_infos.push_back({"set_item_flags", "Enable or disable persistent (authored) item flags by name on scene items. Mesh-scoped flags (lightmapped, shadow_cast) go on the Mesh attachment; passing a Node id resolves to its mesh automatically, so get_scene_nodes ids work directly. Flag names match glTF serialization (e.g. \"lightmapped\", \"shadow_cast\", \"visible\").", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"},  {"description", "Name of the scene"}}},
            {"ids",        {{"type", "array"},   {"items", {{"type", "integer"}}}, {"description", "Item ids (node ids resolve to their mesh attachment)"}}},
            {"flags",      {{"type", "array"},   {"items", {{"type", "string"}}}, {"description", "Persistent flag names to change"}}},
            {"enabled",    {{"type", "boolean"}, {"description", "true to enable the flags (default), false to disable"}}}
        }},
        {"required", json::array({"scene_name", "ids", "flags"})}
    }});
    m_tool_infos.push_back({"lightmap_frame_selection", "Frame the selected meshes' lightmap UV charts in the Lightmap Texture window: shows the window and sets its pan/zoom so every selected mesh's atlas region is visible. Useful for diagnosing unwrap defects on a specific mesh (pairs with the window's overlap highlight).", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_reorder_charts", "Leak camouflage for per-facet unwraps: async re-prepare of the world-space partition with the piece charts packed in baked-luminance order, so similarly lit facets are atlas neighbors and cross-chart filter-tap / dilation pollution picks up similar values. Requires uv_parameterizer = per_facet, a prepared partition (lightmap_prepare_tiles) and an existing bake. Does NOT bake - the reordered tiles are stale until the next bake (lightmap_set_baking / lightmap_bake_direct); tiles reordered per-tile leave the other tiles' packing unchanged so they restore from disk. Poll get_async_status until idle afterwards.", {
        {"type", "object"},
        {"properties", {
            {"tile",   {{"type", "integer"}, {"description", "Spatial tile index to reorder (from lightmap_get_tiles); omit or -1 = all tiles"}}},
            {"active", {{"type", "boolean"}, {"description", "Reorder only the ACTIVE tiles (the camera-clamped resident set currently gathering); overrides 'tile'"}}}
        }}
    }});

    m_tool_infos.push_back({"lightmap_bake_gbuffer", "Rasterize the lightmap texel G-buffer for the current atlas layout (run lightmap_prepare_tiles first): world position + normal per lightmap texel, one draw per region in atlas UV space. Optionally writes debug PNGs (<base>_position.png, <base>_normal.png).", {
        {"type", "object"},
        {"properties", {
            {"debug_png_base", {{"type", "string"}, {"description", "Optional path base for debug PNGs of the baked G-buffer"}}}
        }}
    }});

    m_tool_infos.push_back({"lightmap_bake_direct", "Bake direct lighting into the lightmap atlas: per valid G-buffer texel, explicit sampling of every scene light with a ray-query shadow ray against all content meshes. Requires lightmap_prepare_tiles + lightmap_bake_gbuffer first. Optionally writes a tone-mapped debug PNG.", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Name of the scene (lights + occluders)"}}},
            {"debug_png",  {{"type", "string"}, {"description", "Optional path for a tone-mapped PNG of the baked lightmap"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});

    m_tool_infos.push_back({"lightmap_bake_to_disk", "Bake every spatial lightmap tile to disk, one tile at a time (bounded memory regardless of world size): per tile a G-buffer raster, offline_sweeps gather sweeps, resolve/denoise/dilate/seam-blend, then tile_<id>.lmt + manifest.json are written into <scene>.lightmap/. Runs to completion inside this call (headless verification). Requires a prepared world-space partition (lightmap_prepare_tiles); the layout is computed automatically if missing. The streamer picks the fresh tiles up when interactive baking is off.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_save_all_tiles", "Write every RESIDENT tile's current published lightmap (interactive bake state) to <scene>.lightmap/ (tile_<id>.lmt + manifest.json) right now. Non-resident tiles have no content in memory - use lightmap_bake_to_disk (UI: 'Batch Process All Tiles') to bake and persist all tiles. Evicted tiles are saved automatically (save-on-evict). Returns how many tiles were saved.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_clear_tiles", "Clear All Tiles: forget all baked lightmap content (display clears to white, accumulation restarts) AND delete the active scene's on-disk tile set (<scene>.lightmap/: manifest.json + tile_*.lmt; other files untouched). Rebake with lightmap_set_baking / lightmap_bake_to_disk. Refuses while the offline bake runs.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_prepare_tiles", "Prepare world-space lightmap tiles: make every lightmapped mesh/primitive instance unique, bake its node transform into the vertices (world space), clip the geometry against the spatial kd tile planes (clip vertices are binary exact across the two tiles sharing a plane) and re-unwrap each piece's channel-2 UVs at world-space density. Pieces become Mesh_primitives of new meshes under the identity 'Lightmap Pieces' group node; originals stay in the scene for lightmap_revert_tiles / re-prepare. Self-contained: the spatial grid split is computed from geometry alone and the per-tile density comes from the grid (tile_texture_size / cell size). On commit the display atlas clears to white and a single bake iteration runs automatically, then pauses (with the pause autosave). ASYNC by default: returns {queued:true} immediately and the heavy phase runs in the background while the old partition stays live; poll get_async_status until pending + running + queued_operations == 0, then read its lightmap_prepare.last_result (a mid-flight primitive swap aborts the commit and keeps the old partition; cancel with lightmap_prepare_cancel). Refuses while async mesh operations or another prepare are in flight. Toggle rendering between originals and pieces with lightmap_set_render.", {
        {"type", "object"},
        {"properties", {
            {"scene_name",           {{"type", "string"},  {"description", "Name of the scene to partition"}}},
            {"tile_texture_size",    {{"type", "integer"}, {"description", "Texel side of one spatial tile (pow2 256..8192); default = Lightmap settings value"}}},
            {"resident_tile_budget", {{"type", "integer"}, {"description", "Resident tile budget; default = Lightmap settings value"}}},
            {"wait",                 {{"type", "boolean"}, {"description", "Block until the partition commits and return the full per-piece manifest (small scenes only: the MCP HTTP response is dropped after 5 s while the work still completes). Default false"}}}
        }},
        {"required", json::array({"scene_name"})}
    }});
    m_tool_infos.push_back({"lightmap_prepare_cancel", "Request cancellation of an in-flight lightmap_prepare_tiles: remaining regions are skipped, the results are discarded and the OLD partition is kept. Does not wait - poll get_async_status until pending drains. Idempotent.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_revert_tiles", "Revert the world-space lightmap partition: destroy the 'Lightmap Pieces' group and restore original mesh visibility. Originals were never modified, so this is always safe.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"lightmap_set_render", "Toggle 'render with lightmaps' for the world-space partition: ON renders the piece meshes (all lightmapped geometry regardless of tile residency; non-resident tiles fall back to white) and hides the originals, OFF restores the originals. Call without 'enabled' to query.", {
        {"type", "object"},
        {"properties", {
            {"enabled", {{"type", "boolean"}, {"description", "Render pieces (true) or originals (false); omit to query"}}}
        }}
    }});
    m_tool_infos.push_back({"lightmap_get_tiles", "List the lightmap quadtree grid tiles of the current layout: per tile its grid key {level, ix, iz} (cells anchored at multiples of the cell size from the world origin; level 0 = lightmap.cell_size_m, +1 halves the cell / doubles density, -1 doubles the cell), cell size, nominal texels/m, down-only density flex, content/residency flags and cell bounds - plus the scene's stored overrides.", schema_no_args()});
    m_tool_infos.push_back({"lightmap_subdivide_tile", "Subdivide one lightmap grid leaf tile {level, ix, iz} into 4 half-size cells (2x nominal texel density). The override persists in the scene (Scene_settings::lightmap_tile_overrides); with a live partition an async re-prepare is launched (poll get_async_status), otherwise the layout updates on the next bake tick. Get keys from lightmap_get_tiles.", {
        {"type", "object"},
        {"properties", {
            {"level", {{"type", "integer"}, {"description", "Quadtree level of the tile to subdivide (from lightmap_get_tiles)"}}},
            {"ix",    {{"type", "integer"}, {"description", "Cell X index at that level"}}},
            {"iz",    {{"type", "integer"}, {"description", "Cell Z index at that level"}}}
        }},
        {"required", json::array({"level", "ix", "iz"})}
    }});
    m_tool_infos.push_back({"lightmap_merge_tile", "Merge one lightmap grid leaf tile {level, ix, iz} AND its 3 siblings into their parent cell (half nominal texel density). All 4 siblings must currently be leaves. The override persists in the scene; with a live partition an async re-prepare is launched (poll get_async_status). Get keys from lightmap_get_tiles.", {
        {"type", "object"},
        {"properties", {
            {"level", {{"type", "integer"}, {"description", "Quadtree level of one of the 4 sibling tiles to merge (from lightmap_get_tiles)"}}},
            {"ix",    {{"type", "integer"}, {"description", "Cell X index at that level"}}},
            {"iz",    {{"type", "integer"}, {"description", "Cell Z index at that level"}}}
        }},
        {"required", json::array({"level", "ix", "iz"})}
    }});
    m_tool_infos.push_back({"lightmap_set_baking", "Toggle the interactive progressive lightmap bake (per-frame budgeted gather with accumulation; direct light + indirect bounces; scene edits restart convergence). enabled:false PAUSES - accumulation and sweep counts are kept, the viewport keeps showing the published bake, the resident tiles autosave to <scene>.lightmap/, and enabled:true continues where it paused (reset is the explicit restart). single_iteration advances the bake until every active tile reaches the current minimum sweep count + 1, then pauses (autosaving too; repeated calls advance all tiles in lockstep). Reports bake status (sweeps completed, cursor row) and can write a tone-mapped debug PNG of the published atlas. Call without 'enabled' to just query status.", {
        {"type", "object"},
        {"properties", {
            {"enabled",          {{"type", "boolean"}, {"description", "Turn interactive baking on/off (off = pause; accumulation kept); omit to leave unchanged"}}},
            {"reset",            {{"type", "boolean"}, {"description", "Restart accumulation (keeps layout + G-buffer)"}}},
            {"single_iteration", {{"type", "boolean"}, {"description", "Bake one more full sweep of every active tile, then pause"}}},
            {"debug_png",        {{"type", "string"},  {"description", "Optional path for a tone-mapped PNG of the published atlas"}}}
        }}
    }});

    // Transform reference frame for the transform gizmo / numeric edits.
    m_tool_infos.push_back({"set_transform_reference_mode", "Set the orientation reference frame (transform space) of the transform tool: global/world (world axes), local (selection's own orientation), reference (a chosen reference node's orientation), or selection (a frame derived from the active mesh-component selection). For reference mode, give reference_node_id or reference_node_name (searched across scenes, or within scene_name when given) - this is how you set the reference. edge_normal_blend tunes the selection-mode frame. Read the current state back with get_transform_state.", {
        {"type", "object"},
        {"properties", {
            {"mode",                {{"type", "string"}, {"enum", json::array({"global", "world", "local", "reference", "selection"})}, {"description", "Transform space / reference mode ('world' is an alias for 'global')"}}},
            {"scene_name",          {{"type", "string"},  {"description", "Scene to resolve reference_node in (optional; otherwise all scenes are searched)"}}},
            {"reference_node_id",   {{"type", "integer"}, {"description", "Reference-mode node ID (sets the reference)"}}},
            {"reference_node_name", {{"type", "string"},  {"description", "Reference-mode node name (sets the reference)"}}},
            {"edge_normal_blend",   {{"type", "number"},  {"description", "Selection mode: [0,1] blend between the two faces sharing a selected edge"}}}
        }},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"get_transform_state", "Read the transform tool's current state: reference frame / transform space (reference_mode: global/local/reference/selection), the chosen reference_node (for reference mode), edge_normal_blend (selection mode), the mesh transform_mode (move/extrude/...), whether a mesh-component selection is driving the gizmo (component_mode), the selected node count, and the resolved world-space anchor_frame (origin + orientation the gizmo and local-space numeric edits operate in). Complements set_transform_reference_mode / set_transform_mode.", schema_no_args()});
    m_tool_infos.push_back({"set_transform_mode", "Set what the transform tool does to a mesh-component (vertex/edge/face) selection when the gizmo is dragged: move (translate/rotate/scale in place), extrude (duplicate the selection boundary, bridge it with new faces, then move along the gizmo delta), extrude_group_normal (same topology, but each disjoint selection subset slides along its own average normal by an amount derived from the drag), or extrude_vertex_normal (same topology, but each vertex slides along its own normal). Persisted in editor settings; applies to subsequent component edits.", {
        {"type", "object"},
        {"properties", {
            {"mode", {{"type", "string"}, {"enum", json::array({"move", "extrude", "extrude_group_normal", "extrude_vertex_normal"})}, {"description", "Mesh transform mode"}}}
        }},
        {"required", json::array({"mode"})}
    }});
    m_tool_infos.push_back({"set_gizmo_visibility", "Show or hide the transform gizmo handle sets (translate arrows, rotate rings, scale handles) and choose the scale gizmo style (basic axis/plane/uniform handles, or bounding_box face cones). This is the scriptable equivalent of activating the Move/Rotate/Scale tool from the hotbar or toggling the viewport-toolbar gizmo buttons, which are otherwise mouse-only. Omitted flags keep their current value; returns the resulting state.", {
        {"type", "object"},
        {"properties", {
            {"translate",        {{"type", "boolean"}, {"description", "Show the translate handles"}}},
            {"rotate",           {{"type", "boolean"}, {"description", "Show the rotate rings"}}},
            {"scale",            {{"type", "boolean"}, {"description", "Show the scale handles"}}},
            {"scale_gizmo_mode", {{"type", "string"}, {"enum", json::array({"basic", "bounding_box"})}, {"description", "Scale gizmo style"}}}
        }}
    }});

    // Geometry node graph (Geometry Graph window)
    m_tool_infos.push_back({"get_geometry_graph", "List the geometry node graph the Geometry Graph window currently targets: nodes with ids, type labels, input/output pins (slot, key, name, connection count), per-output payload summaries (geometry vertex/facet counts, point/instance counts, scalar values) and all links. 'selected' is true when the window has a target (a Graph Mesh asset). Waits for any background graph evaluation to finish, so the payloads reflect all previously issued mutations.", schema_no_args()});
    m_tool_infos.push_back({"set_geometry_graph_target", "Point the Geometry Graph window (and the geometry_graph_* tools) at a Graph Mesh asset by name, or clear the target with an empty/omitted name. This replaces the old 'select the asset to edit it' behaviour - the window no longer follows the global selection (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to target; empty or omitted clears the target"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_add_node", "Add a node to the geometry node graph. Returns the new node's id and pin layout.", {
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"}, {"enum", json::array({"box", "sphere", "torus", "cone", "disc", "subdivide", "conway", "transform", "lattice", "triangulate", "normalize", "reverse", "repair", "join", "boolean", "float", "integer", "vector", "math", "passthrough", "output"})}, {"description", "Node type to create"}}}
        }},
        {"required", json::array({"type"})}
    }});
    m_tool_infos.push_back({"geometry_graph_connect", "Connect an output pin of one geometry graph node to an input pin of another (pins are addressed by node id + pin slot index; pin keys must match). Undoable; the graph re-evaluates in the background (get_geometry_graph waits for completion, get_async_status reports progress).", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_disconnect", "Disconnect a geometry graph link (addressed like geometry_graph_connect). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_remove_node", "Remove a geometry graph node (its links are removed too). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to remove"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_parameter", "Set parameters of a geometry graph node. Takes the same JSON object shape as the graph file's per-node 'parameters' (see get_geometry_graph); partial updates are allowed - omitted keys keep their current values. Undoable; the graph re-evaluates in the background (get_geometry_graph waits for completion, get_async_status reports progress).", {
        {"type", "object"},
        {"properties", {
            {"node_id",    {{"type", "integer"}, {"description", "Id of the node"}}},
            {"parameters", {{"type", "object"},  {"description", "Parameter key/values to set"}}}
        }},
        {"required", json::array({"node_id", "parameters"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_display_flags", "Set or clear a geometry graph node's display / ghost designation (Houdini display / template flags). The display node's geometry replaces the output node's wired input in the scene bake (render mesh AND physics); the ghost node's geometry additionally shows as a dim edge-lines-only mesh (no shadow, not pickable). One node each per graph: setting a flag moves it from any previous holder; false clears the flag only when this node holds it. Undoable; the graph re-bakes in the background (get_geometry_graph waits and reports display_node_id / ghost_node_id).", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to (un)designate"}}},
            {"display", {{"type", "boolean"}, {"description", "true: make this the display node; false: clear the display flag if this node holds it"}}},
            {"ghost",   {{"type", "boolean"}, {"description", "true: make this the ghost node; false: clear the ghost flag if this node holds it"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_node_previews", "Enable/disable per-node mesh preview thumbnails on the geometry graph canvas (the 'Show node previews' checkbox; editor-global persistent setting, ON by default). Enabling forces a full background re-evaluation of every graph so every node gets a preview; previews render a few nodes per frame after evaluation completes.", {
        {"type", "object"},
        {"properties", {
            {"enabled", {{"type", "boolean"}, {"description", "true (default) to show previews, false to hide"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_set_link_mid_points", "Set (or clear) the canvas routing mid points of a geometry graph link, optionally with explicit pen-tool tangent handles per point. Mid points are canvas-space control points the link's wire is routed through (on-canvas gestures: double-click a link to add one, double-click a handle to remove it, drag a handle to move it; on a SELECTED link each point shows tangent dots - dragging one captures the computed tangents and switches the point to mirrored, Alt-drag breaks it to free, double-click on a dot resets to auto). The link is identified by its endpoint pins (see get_geometry_graph 'links'); the whole mid point list is replaced. Omitted / empty mid_points clears the routing (plain direct wire). Canvas-only state (like node positions): not undoable, not saved to the scene, but carried by canvas copy / paste. get_geometry_graph reports each link's current 'mid_points' in the same dual entry form.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the source (output) node"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the sink (input) node"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot on the sink node (default 0)"}}},
            {"mid_points",     {{"type", "array"},   {"description", "Control points ordered from source to sink; empty / omitted clears. Each entry is a canvas-space [x, y] pair (Auto tangents) or an object {pos:[x,y], mode:1|2|3, in:[x,y], out:[x,y]} with explicit pen-tool tangent offsets (mode 1 mirrored, 2 aligned, 3 free)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_link_curve", "Set a geometry graph link's per-link curve shape (Kochanek-Bartels tension / continuity / bias, each clamped to [-1, 1]; all 0 = the default routing). Tension scales tangent lengths (+1 makes the link a polyline, negative swings wider); continuity and bias reshape the tangents at the link's routing mid points (no visible effect on a link without mid points). Canvas-only state (like mid points): not undoable, not saved to the scene, but carried by canvas copy / paste. Also editable in the Node Properties window when the link is selected on the canvas. get_geometry_graph reports each link's non-default params as 'curve' [tension, continuity, bias].", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the source (output) node"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the sink (input) node"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot on the sink node (default 0)"}}},
            {"tension",        {{"type", "number"},  {"description", "Tangent length scale, -1..1 (default 0; +1 = polyline, negative = wider swing)"}}},
            {"continuity",     {{"type", "number"},  {"description", "Tangent split at mid points, -1..1 (default 0; +/-1 = corners)"}}},
            {"bias",           {{"type", "number"},  {"description", "Tangent lean at mid points, -1..1 (default 0; +1 leans towards the incoming chord)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"geometry_graph_set_view", "Show the Geometry Graph window and set its node-editor zoom (view scale) immediately, centered on the graph content. zoom > 1 zooms in (content drawn larger), zoom < 1 zooms out. Deterministic (no animation / no mouse input) - intended for headless zoom-quality verification (capture_screenshot on the next frame). Requires the window to have a target Graph Mesh (create_graph_mesh / set_geometry_graph_target) for it to render nodes.", {
        {"type", "object"},
        {"properties", {
            {"zoom", {{"type", "number"}, {"description", "View scale (> 0). 1.0 = native, 2.0 = 2x zoom in, 0.5 = 2x zoom out."}}}
        }},
        {"required", json::array({"zoom"})}
    }});
    m_tool_infos.push_back({"texture_graph_add_all", "Add one node of every texture graph node type to the selected Graph Texture, laid out in a grid. Same action as the canvas background context menu's \"Add all\"; adds as a single compound undo entry. Intended for visual verification - it puts the whole node library on the canvas at once, where a node that fails to compose, renders black or mis-lays-out its widgets is obvious. The grid layout applies once the canvas has measured every node, so render a few frames (or texture_graph_set_view + capture_screenshot) before judging positions.", {
        {"type", "object"},
        {"properties", json::object()}
    }});
    m_tool_infos.push_back({"texture_graph_set_view", "Show the Texture Graph window and set its node-editor zoom (view scale) immediately, centered on the graph content. zoom > 1 zooms in (content drawn larger), zoom < 1 zooms out. Deterministic (no animation / no mouse input) - intended for headless zoom-quality verification (capture_screenshot on the next frame), e.g. checking that in-node widgets (gradient bar, curve box) scale with the zoom. Requires the window to have a target Graph Texture (create_graph_texture) for it to render nodes.", {
        {"type", "object"},
        {"properties", {
            {"zoom", {{"type", "number"}, {"description", "View scale (> 0). 1.0 = native, 2.0 = 2x zoom in, 0.5 = 2x zoom out."}}}
        }},
        {"required", json::array({"zoom"})}
    }});
    m_tool_infos.push_back({"geometry_graph_select_nodes", "Set the Geometry Graph window's canvas selection: clears the current canvas selection and selects the given node ids (empty / omitted node_ids just clears) and optionally a link. A selected link shows its pen-tool tangent dots on the canvas and a link section in Node Properties. Also shows the Geometry Graph and Node Properties windows so the selection and its properties are observable in the next capture_screenshot. Nodes must have rendered at least once (show the window first, e.g. geometry_graph_set_view).", {
        {"type", "object"},
        {"properties", {
            {"node_ids", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Ids of the nodes to select (see get_geometry_graph); empty clears the selection"}}},
            {"link",     {{"type", "object"}, {"description", "Link to select, identified by its endpoint pins: {source_node_id, source_slot, sink_node_id, sink_slot} (slots default 0)"}}}
        }}
    }});
    m_tool_infos.push_back({"texture_graph_select_nodes", "Set the Texture Graph window's canvas node selection: clears the current canvas selection and selects the given node ids (empty / omitted node_ids just clears). Also shows the Texture Graph and Node Properties windows so the selection and its properties are observable in the next capture_screenshot. Nodes must have rendered at least once (show the window first).", {
        {"type", "object"},
        {"properties", {
            {"node_ids", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Ids of the nodes to select (see get_texture_graph); empty clears the selection"}}}
        }}
    }});
    m_tool_infos.push_back({"geometry_graph_set_node_layout", "Set a geometry graph node's canvas layout: position, requested width / height (canvas units; content is not scaled - the node gets more room; 0 = automatic, content taller/wider than the request wins), and which node edge the input / output pins are laid out on. All arguments except node_id are optional; omitted ones are left unchanged. Returns the resulting layout. Same knobs as the Node Properties window's Position / Size / Inputs / Outputs rows.", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node (see get_geometry_graph)"}}},
            {"position",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "Canvas position [x, y]"}}},
            {"width",       {{"type", "number"},  {"description", "Requested node width in canvas units; 0 = automatic (content-derived)"}}},
            {"height",      {{"type", "number"},  {"description", "Requested node height in canvas units; 0 = automatic (content-derived)"}}},
            {"pin_label_width", {{"type", "number"}, {"description", "Pin label column width in canvas units (default 70; clamped to 20..400)"}}},
            {"input_edge",  {{"type", "string"},  {"description", "Edge for input pins: left (default) or right"}}},
            {"output_edge", {{"type", "string"},  {"description", "Edge for output pins: left or right (default)"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_set_node_layout", "Set a texture graph node's canvas layout: position, requested width / height (canvas units; content is not scaled - the node gets more room; 0 = automatic, content taller/wider than the request wins), and which node edge the input / output pins are laid out on. All arguments except node_id are optional; omitted ones are left unchanged. Returns the resulting layout. Same knobs as the Node Properties window's Position / Size / Inputs / Outputs rows.", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node (see get_texture_graph)"}}},
            {"position",    {{"type", "array"},   {"items", {{"type", "number"}}}, {"description", "Canvas position [x, y]"}}},
            {"width",       {{"type", "number"},  {"description", "Requested node width in canvas units; 0 = automatic (content-derived)"}}},
            {"height",      {{"type", "number"},  {"description", "Requested node height in canvas units; 0 = automatic (content-derived)"}}},
            {"pin_label_width", {{"type", "number"}, {"description", "Pin label column width in canvas units (default 70; clamped to 20..400)"}}},
            {"input_edge",  {{"type", "string"},  {"description", "Edge for input pins: left (default) or right"}}},
            {"output_edge", {{"type", "string"},  {"description", "Edge for output pins: left or right (default)"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"create_graph_texture", "Create a Graph Texture asset (a procedural texture backed by a node graph) in a scene's content library and point the Texture Graph window at it (its new target). The window's target Graph Texture is what the texture_graph_* tools operate on (retarget later with set_texture_graph_target). A Material slot can then source from it (set_material_texture_source). Returns the new asset's id and name.", {
        {"type", "object"},
        {"properties", {
            {"name",       {{"type", "string"}, {"description", "Name of the new Graph Texture asset (must be unique in the scene)"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"name"})}
    }});
    m_tool_infos.push_back({"set_material_texture_source", "Bind a Material texture slot to a Graph Texture asset so the material samples the graph's baked output (the material->graph back-reference; editing the graph updates the material live). Each slot holds a single texture reference, so binding replaces any previously assigned texture. Omit or empty 'graph_texture' to clear the slot entirely.", {
        {"type", "object"},
        {"properties", {
            {"material_name", {{"type", "string"}, {"description", "Name of the material in the scene's content library"}}},
            {"slot",          {{"type", "string"}, {"description", "Texture slot: base_color (default), metallic_roughness, normal, occlusion, or emissive"}}},
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to source from; empty to clear the binding"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"material_name"})}
    }});
    m_tool_infos.push_back({"get_graph_textures", "List the Graph Texture assets in a scene's content library (or all scenes when scene_name is omitted): name, id, scene, node_count, and has_output (whether the graph currently bakes an output texture).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Scene to list (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"create_graph_mesh", "Create a Graph Mesh asset (a procedural mesh backed by a geometry node graph) in a scene's content library and point the Geometry Graph window at it (its new target). The window's target Graph Mesh is what the geometry_graph_* tools operate on (retarget later with set_geometry_graph_target). A scene Node can then source its mesh from it (set_node_graph_mesh). Returns the new asset's id and name.", {
        {"type", "object"},
        {"properties", {
            {"name",       {{"type", "string"}, {"description", "Name of the new Graph Mesh asset (must be unique in the scene)"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"name"})}
    }});
    m_tool_infos.push_back({"set_node_graph_mesh", "Bind a scene Node to a Graph Mesh asset via a Geometry Graph Mesh attachment: the node's renderable mesh (and optional physics) is sourced from the graph's baked output, and the attachment points back at the asset (the node->graph back-reference; editing the graph updates the node's mesh live). Creates the attachment when missing; omit or empty 'graph_mesh' to remove the attachment and its controlled mesh. Scene state reflects the bake after the next evaluation completes (get_geometry_graph is the barrier).", {
        {"type", "object"},
        {"properties", {
            {"node_name",  {{"type", "string"}, {"description", "Name of the scene node to bind"}}},
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to source from; empty to remove the binding"}}},
            {"scene_name", {{"type", "string"}, {"description", "Target scene (default: the single/first scene)"}}}
        }},
        {"required", json::array({"node_name"})}
    }});
    m_tool_infos.push_back({"get_graph_meshes", "List the Graph Mesh assets in a scene's content library (or all scenes when scene_name is omitted): name, id, scene, node_count, baked_revision and has_bake (whether the graph has published baked products).", {
        {"type", "object"},
        {"properties", {
            {"scene_name", {{"type", "string"}, {"description", "Scene to list (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"get_texture_graph", "List the procedural texture node graph the Texture Graph window currently targets: the target asset name/id, nodes with ids, type labels, canvas positions, parameters, input pins (slot, value type, whether connected, source node id/slot) and output pins (slot, value type), plus a 'composable' flag per node, and all links. 'selected' is true when the window has a target. Texture graph evaluation is synchronous, so no wait is needed.", schema_no_args()});
    m_tool_infos.push_back({"set_texture_graph_target", "Point the Texture Graph window (and the texture_graph_* tools) at a Graph Texture asset by name, or clear the target with an empty/omitted name. Replaces the old selection-driven targeting - the window no longer follows the global selection (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to target; empty or omitted clears the target"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    // Build the node-type enum from the descriptor registry (plus the bespoke
    // "output" sink) so it never drifts as node types are added.
    json texture_node_type_enum = json::array();
    for (const erhe::texgen::Node_descriptor* descriptor : all_texture_node_descriptors()) {
        texture_node_type_enum.push_back(descriptor->name);
    }
    texture_node_type_enum.push_back("output");
    texture_node_type_enum.push_back("material_output");
    m_tool_infos.push_back({"texture_graph_add_node", "Add a node to the texture node graph. Returns the new node's id, parameters and pin layout.", {
        {"type", "object"},
        {"properties", {
            {"type", {{"type", "string"}, {"enum", texture_node_type_enum}, {"description", "Node type to create"}}},
            {"position", {{"type", "array"}, {"items", {{"type", "number"}}}, {"description", "Optional [x, y] canvas position; defaults to the next spawn-grid slot"}}}
        }},
        {"required", json::array({"type"})}
    }});
    m_tool_infos.push_back({"texture_graph_connect", "Connect an output pin of one texture graph node to an input pin of another (pins are addressed by node id + pin slot index; pin keys / value types must match). Undoable. Returns an error result when the pin keys differ or the link would create a cycle.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_disconnect", "Disconnect a texture graph link (addressed like texture_graph_connect). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"source_node_id", {{"type", "integer"}, {"description", "Id of the node providing the output"}}},
            {"source_slot",    {{"type", "integer"}, {"description", "Output pin slot index on the source node (default 0)"}}},
            {"sink_node_id",   {{"type", "integer"}, {"description", "Id of the node receiving the input"}}},
            {"sink_slot",      {{"type", "integer"}, {"description", "Input pin slot index on the sink node (default 0)"}}}
        }},
        {"required", json::array({"source_node_id", "sink_node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_remove_node", "Remove a texture graph node (its links are removed too). Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the node to remove"}}}
        }},
        {"required", json::array({"node_id"})}
    }});
    m_tool_infos.push_back({"texture_graph_set_parameter", "Set parameters of a texture graph node. Takes the same JSON object shape as the graph file's per-node 'parameters' (see get_texture_graph); partial updates are allowed - omitted keys keep their current values. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"node_id",    {{"type", "integer"}, {"description", "Id of the node"}}},
            {"parameters", {{"type", "object"},  {"description", "Parameter key/values to set"}}}
        }},
        {"required", json::array({"node_id", "parameters"})}
    }});
    m_tool_infos.push_back({"texture_graph_export_png", "Compose and render a texture graph node's output to a PNG file. For a generator/filter node the given output slot is rendered; for the Output node (no output pins) the connected input's subtree is rendered. Returns the written path and its width/height. Requires the graphics device (works in the headless Vulkan build).", {
        {"type", "object"},
        {"properties", {
            {"node_id",     {{"type", "integer"}, {"description", "Id of the node whose output to render"}}},
            {"output_slot", {{"type", "integer"}, {"description", "Output pin slot index to render (default 0; ignored for the Output node)"}}},
            {"size",        {{"type", "integer"}, {"description", "Square edge length in pixels (default 256, clamped to [1, 4096])"}}},
            {"path",        {{"type", "string"},  {"description", "PNG file path to write (parent directories are created)"}}}
        }},
        {"required", json::array({"node_id", "path"})}
    }});
    m_tool_infos.push_back({"texture_graph_export_material", "Compose and render each connected PBR channel of a Material Output node to a PNG file in 'dir': <base_name>_albedo.png, _normal.png, _emissive.png, _occlusion.png, and _metallic_roughness.png (glTF ORM packing: R=occlusion, G=roughness, B=metallic). Only connected channels are written. Returns the list of written file paths. Requires the graphics device (works in the headless Vulkan build).", {
        {"type", "object"},
        {"properties", {
            {"node_id", {{"type", "integer"}, {"description", "Id of the Material Output node"}}},
            {"dir",     {{"type", "string"},  {"description", "Output directory (created if missing); files are named <base_name>_<channel>.png"}}},
            {"size",    {{"type", "integer"}, {"description", "Square edge length in pixels (default 256, clamped to [1, 4096])"}}}
        }},
        {"required", json::array({"node_id", "dir"})}
    }});

    // Extra editor window instances (issue #252). Open additional Geometry
    // Graph / Texture Graph / Properties windows, each with its own target,
    // so several assets can be edited / inspected side by side. The user
    // closes them with the window X button.
    m_tool_infos.push_back({"open_geometry_graph_window", "Open a new Geometry Graph window (in addition to the primary one), optionally targeting a named Graph Mesh asset. Multiple windows can target different assets at once (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_mesh", {{"type", "string"}, {"description", "Name of the Graph Mesh asset to target; empty/omitted opens an empty editor"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"open_texture_graph_window", "Open a new Texture Graph window (in addition to the primary one), optionally targeting a named Graph Texture asset. Multiple windows can target different assets at once (issue #252).", {
        {"type", "object"},
        {"properties", {
            {"graph_texture", {{"type", "string"}, {"description", "Name of the Graph Texture asset to target; empty/omitted opens an empty editor"}}},
            {"scene_name",    {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});
    m_tool_infos.push_back({"open_properties_window", "Open a new Properties window pinned to a named material (issue #252). A pinned Properties window shows only its target, independent of the global selection; omit 'material' to open one that follows the selection like the primary Properties window.", {
        {"type", "object"},
        {"properties", {
            {"material",   {{"type", "string"}, {"description", "Name of the material to pin the window to; empty/omitted follows the global selection"}}},
            {"scene_name", {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}}
        }}
    }});

    // Animation timeline / curve editor tools (issue #243). The Animation
    // window and Animation_player share one target animation;
    // set_animation_target points both at a named animation from a scene's
    // content library (glTF import fills it).
    m_tool_infos.push_back({"get_scene_animations", "List animations in scene content libraries (glTF imports land there): per animation the time range and every channel (target node, path, interpolation, keyframe count, sampler index).", schema_scene_name()});
    m_tool_infos.push_back({"set_animation_target", "Point the Animation window (curve editor) and the animation player at a named animation and show the window. Optionally restrict which channels' curves are visible (deterministic view for screenshots). Empty/omitted 'animation' clears the target.", {
        {"type", "object"},
        {"properties", {
            {"animation",        {{"type", "string"}, {"description", "Animation name; empty/omitted clears the target"}}},
            {"scene_name",       {{"type", "string"}, {"description", "Scene to search (default: all scenes)"}}},
            {"visible_channels", {{"type", "array"}, {"items", {{"type", "integer"}}}, {"description", "Channel indices whose curves are visible (all components); other channels are hidden. Omit to keep defaults. Also re-frames the view."}}}
        }}
    }});
    m_tool_infos.push_back({"animation_playback", "Control animation playback (Animation_player): play / pause / stop, seek to a time, set speed, looping and the autokey mode. Applies the sampled pose to the target nodes immediately, so a capture_screenshot on the next frame shows it. Optionally retargets to a named animation first.", {
        {"type", "object"},
        {"properties", {
            {"animation",  {{"type", "string"},  {"description", "Animation name to target first (optional; default: current target)"}}},
            {"scene_name", {{"type", "string"},  {"description", "Scene to search when 'animation' is given (default: all scenes)"}}},
            {"action",     {{"type", "string"},  {"description", "play, pause or stop (optional)"}}},
            {"time",       {{"type", "number"},  {"description", "Seek to this absolute animation time in seconds (optional)"}}},
            {"speed",      {{"type", "number"},  {"description", "Playback speed factor; negative plays backwards (optional)"}}},
            {"looping",    {{"type", "boolean"}, {"description", "Loop at the end of the range (optional)"}}},
            {"autokey",    {{"type", "string"},  {"description", "Autokey mode: off, modified (key only edited paths) or all (key T+R+S on any edit). Applies to finished Transform tool edits, including transform_selection (optional)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_create_key", "Create keys (LightWave 'Create Key') on the targeted animation at a time: writes the nodes' current translation / rotation / scale values, creating missing channels (LINEAR) on demand. Nodes default to the current selection. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"nodes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Node names to key (default: current selection)"}}},
            {"paths", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Paths to key: translation, rotation, scale (default: all three)"}}},
            {"time",  {{"type", "number"}, {"description", "Key time in seconds (default: current play position)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_delete_key", "Delete keys (LightWave 'Delete Key') from the targeted animation: removes the keys at (approximately) a time from every channel targeting the given nodes. Nodes default to the current selection. Undoable.", {
        {"type", "object"},
        {"properties", {
            {"nodes", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "Node names (default: current selection)"}}},
            {"time",  {{"type", "number"}, {"description", "Key time in seconds (default: current play position)"}}}
        }}
    }});
    m_tool_infos.push_back({"animation_edit_keyframe", "Edit animation keyframes (undoable through the operation stack, same code path as the Animation window). edit=move changes a key's time (clamped between neighbor keys) and/or one component's value; edit=insert adds a key at a time, evaluating the curve so the shape is preserved; edit=delete removes a key (all components). Times/values apply to the channel's sampler, so channels sharing a sampler are affected together.", {
        {"type", "object"},
        {"properties", {
            {"animation",     {{"type", "string"},  {"description", "Animation name (optional; default: the Animation window's target)"}}},
            {"scene_name",    {{"type", "string"},  {"description", "Scene to search when 'animation' is given (default: all scenes)"}}},
            {"edit",          {{"type", "string"},  {"description", "move, insert or delete"}}},
            {"channel_index", {{"type", "integer"}, {"description", "Channel index (from get_scene_animations)"}}},
            {"key_index",     {{"type", "integer"}, {"description", "Keyframe index (move / delete)"}}},
            {"time",          {{"type", "number"},  {"description", "move: new time for the key; insert: time of the new key"}}},
            {"value",         {{"type", "number"},  {"description", "move: new value for one component (requires 'component')"}}},
            {"component",     {{"type", "integer"}, {"description", "Component index (0=X, 1=Y, 2=Z, 3=W) for 'value'"}}}
        }},
        {"required", json::array({"edit", "channel_index"})}
    }});

    // Editor commands
    const auto& registered_commands = m_commands.get_commands();
    for (const auto* command : registered_commands) {
        const char* name = command->get_name();
        if (name == nullptr || name[0] == '\0') {
            continue;
        }
        m_tool_infos.push_back({name, std::string{"Editor command: "} + name, schema_no_args()});
    }
}


} // namespace editor
