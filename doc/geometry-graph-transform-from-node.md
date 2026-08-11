# Geometry graph: `transform_from_node` — transform sourced from a scene node

Status: IMPLEMENTED 2026-08-11 per this design (node type
`transform_from_node`; MCP-verified: identity passthrough, drive-by-name,
live re-evaluation on driver move, parameter round trip, clear, and a full
scene save/load round trip with reference re-resolution). The optional items
(uid-in-key persistence, mat4 output pin, Lattice UI refactor to
`item_reference_imgui`) remain open.

Goal: a new geometry-graph node that takes its transform from a scene-graph
node. The geometry graph lives in the Content library, which is owned by the
scene (`Content_library::set_owner` = the `Scene_root`), so graph nodes can
reach the scene and reference nodes in it. The transform-source scene node
must be assignable by drag-and-drop from the Hierarchy onto the graph node.

## Headline finding

**Every hard sub-problem is already solved in the codebase, in one place:
`Lattice_node`** (`src/editor/geometry_graph/nodes/lattice_node.{hpp,cpp}`)
already holds a drag-and-drop scene-node reference (its "transform driver"),
persists it, resolves it lazily, tracks it live and re-evaluates the graph
when the driven node moves. `transform_from_node` is a small new node that
reuses that exact machinery; no new infrastructure is required.

The proven building blocks:

- **Reference type**: `Asset_reference` + `Asset_key{scope = scene_local,
  type = Asset_type::node, name = <node name>}`
  (`src/editor/assets/asset_reference.{hpp,cpp}`, `asset_key.hpp:38` — the
  `node` asset type exists specifically for "graph transform-driver
  references, e.g. Lattice_node"). Resolution walks open scenes by node name
  (`asset_manager.cpp:476-486`); `scene_local` misses do NOT latch, so
  callers retry per frame. `resolve()` self-heals a glTF uid into the key.
- **Scene access from a graph node** (main thread only):
  `get_hosting_scene_root(get_owning_graph_mesh().get())`
  (`scene_root.cpp:1701`) — the Content-library item's `Item_host` IS the
  owning `Scene_root`. Null on shadow clones by construction.
- **Threading contract** (`geometry_graph_node.hpp:93-111`): graphs evaluate
  async on shadow clones; `evaluate()` must never touch live scene state.
  Live scene reads happen in `update_live()` (per-frame main-thread hook,
  driven for every `Graph_mesh` in every scene by
  `Geometry_graph_window::update_live_nodes()`), are cached in a member, and
  `capture_evaluation_state()` copies the cache onto the shadow clone.
- **Live invalidation**: `Lattice_node::update_live()` re-captures the
  driver's matrix, compares, and `mark_dirty()`s on change — dragging the
  driver in the viewport re-evaluates the graph. Do NOT use
  `Node_touched_message`: it fires only at gizmo-drag END / undo, not during
  drags or physics; polling the matrix in `update_live()` is the
  established mechanism. (Cheap-compare option: the transform-serial
  `node_data.transforms.world_from_node_serial` funneled through
  `Node::handle_transform_update`, `node.cpp:341` — a uint64 compare instead
  of a mat4 compare.)
- **Drag-and-drop**: Hierarchy drag payload = type string
  `erhe::scene::Node::static_type_name` ("Node"), data =
  `erhe::Item_base*` (`item_tree_window.cpp:405-425`). Lattice accepts it
  with a hand-rolled `BeginDragDropTarget()` inside the node's `imgui()` on
  the canvas (`lattice_node.cpp:244-278`) — proving canvas drop targets
  work. The reusable `editor::item_reference_imgui` widget
  (`windows/item_reference.{hpp,cpp}`, issue #231) speaks the same payload
  contract, self-scales inside the node table cell (uses
  `GetContentRegionAvail`), and adds the highlight rect/icon/clear button —
  use it instead of hand-rolling; keep `options.candidates` empty on the
  canvas until the ax::NodeEditor popup Suspend/Resume question is settled.
- **Serialization**: graph JSON is written in the export collect phase
  BEFORE glTF node indices exist (`gltf_extensions_export.cpp:466-475`), so
  node parameters cannot carry a glTF node index — references persist as a
  NAME string inside the node's `parameters` (Lattice writes
  `out["transform_node"] = key.name` unconditionally, even unresolved — "no
  silent loss"; `read_parameters` only stores the key, no manager access,
  because it can run off the main thread during shadow snapshots).
- **MCP**: `geometry_graph_set_parameter` passes opaque JSON to
  `read_parameters`, so setting the reference by node name works with zero
  MCP-side changes. Only `geometry_graph_add_node`'s `type` enum
  (`mcp_server_tool_list.cpp:1271`) must gain the new type name — a step the
  attribute-projection docs omit.

## Node design

### Shape (primary): geometry in → geometry out

```
transform_from_node
  inputs : geometry "in"
  params : transform_node (scene node name, drag-and-drop), space (enum)
  outputs: geometry "out"
```

Mirrors `Transform_node::evaluate` (`transform_node.cpp:25-68`) but composes
the CAPTURED scene-node matrix instead of authored TRS: identity →
copy-on-write passthrough; otherwise `copy_with_transform`. Applying the
matrix directly avoids TRS decomposition entirely (no Euler ambiguity, no
negative-scale/skew loss).

Rejected alternative: a pure value node emitting vec3
translation/rotation/scale into the existing `Transform_node` pins. Its
rotation pin is Euler-only (`vec3`, Z·Y·X), so a general matrix would have to
survive an Euler decomposition. A `mat4` OUTPUT pin can be added later —
`mat4_value` pin key and payload accessor exist but have zero users today —
as a secondary output if graphs want raw matrices; `Transform_node` would
need a mat4 input pin to consume it.

### Space semantics

Parameter `space` (int enum, serialized):

- `0 = local` (default): `node->parent_from_node()` — matches Lattice's
  deliberate choice; parent the driver under the bound mesh node and the
  transform composes in the graph output's local frame.
- `1 = world`: `node->world_from_node()` — for drivers living elsewhere in
  the scene. Caveat documented in the node UI: the graph output is baked into
  the bound node's local space, so a world-space driver double-transforms if
  the bound node itself is not at identity.

"Relative to the bound node" is intentionally NOT offered: a graph asset can
be bound to 0..N scene nodes (`apply_baked_products_to_attachments` sweeps
all scenes), so there is no unique bound node to be relative to.

### Lifecycle / threading (copy Lattice verbatim)

- Members: `App_context& m_context` (factory passes it, like `Lattice_node`),
  `Asset_reference m_node_reference` (+ `set_user_label` in ctor),
  `glm::mat4 m_captured_transform{1.0f}`, `int m_space{0}`.
- `update_live()`: resolve (retry — misses don't latch) + capture matrix per
  `m_space` + `mark_dirty()` on change. `prepare_for_evaluation()` =
  `update_live()`. `capture_evaluation_state()` copies
  `m_captured_transform` (+ space) from the live node; shadows never resolve.
- `evaluate()`: reads only `m_captured_transform`.
- Deletion semantics: same as Lattice — `Asset_reference` holds a strong
  `shared_ptr`, so a deleted driver keeps feeding its last (frozen) transform
  this session and shows `"(unresolved: <name>)"` after reload. No
  node-removal bus message exists; do not invent one for this feature.

### Serialization

`parameters` JSON: `{"transform_node": "<name>", "space": 0}` — written
unconditionally (also while unresolved/empty); `read_parameters` builds the
`Asset_key` only and resets the captured matrix when the name changes, then
`mark_dirty()` (mandatory convention). This makes
`geometry_graph_set_parameter {"transform_node": "Driver"}` work from MCP
for free, per `doc/mcp_api_guidelines.md` (explicit args, echo effective
values — the handler already returns the node JSON).

**Known weakness + optional improvement**: name-only keys break on rename and
are ambiguous under duplicate names (resolution takes the first match in
deterministic order, debug-logs ambiguity). Scene nodes DO have a stable
persistent id — the glTF 2.1 `uid` (`Item_base::get_gltf_uid()`, stamped at
export). Improvement: also write `"transform_node_uid"` and prefer uid on
load (matching `ERHE_asset_reference`'s "uid first, name fallback" doctrine).
If adopted, upgrade `Lattice_node` in the same commit so both reference
holders behave identically. Caveat: uid is empty for nodes never yet saved.

### UI

In `imgui()` (canvas + Node Properties both route through it):

```cpp
std::shared_ptr<erhe::Item_base> value = m_node_reference.get();
Item_reference_options options; options.none_text = "(drop a scene node)";
ImGui::TextUnformatted("Transform node");
if (item_reference_imgui(m_context, "##transform_node", value, erhe::scene::Node::get_static_type(), options)) {
    set_transform_node(std::dynamic_pointer_cast<erhe::scene::Node>(value));
}
```

plus an `imgui_enum_combo("space", ...)` (scale-aware shared widget). The
drop commits an undoable `Geometry_graph_parameter_operation` automatically:
`mark_dirty()` inside `imgui()` sets `m_parameter_edit_in_progress`, and the
commit check fires the same frame because nothing stays active after a drop
(`graph_editor_node.cpp:178-193, 285-294`) — verified behavior on Lattice.

Follow-up cleanup (separate commit): replace Lattice's hand-rolled drop
target with the same `item_reference_imgui` call.

## Implementation checklist

1. `src/editor/geometry_graph/nodes/transform_from_node.{hpp,cpp}` — pins,
   params, `evaluate`, `imgui`, `write/read_parameters`, live hooks.
2. `src/editor/CMakeLists.txt` — add the file pair (alphabetical block).
3. `geometry_graph_node_factory.cpp` — `"transform_from_node"`, ctor takes
   `App_context&` (like `lattice`).
4. `geometry_graph_window.cpp build_palette()` — entry; either under
   "Operations" next to Transform, or start a "Scene" category (source nodes
   `brush`/`scene_mesh` could migrate there later).
5. `mcp_server_tool_list.cpp:1271` — add to the `geometry_graph_add_node`
   type enum (docs omit this step; without it MCP cannot create the node).
6. Optional: uid-in-key persistence (+ same for `Lattice_node`).
7. Optional follow-up: `mat4` output pin + `Transform_node` mat4 input (first
   users of the dormant `mat4_value` pin key; check
   `Geometry_payload::operator+=` multi-link semantics for mat4 first).

## Verification plan

- In-editor via MCP (Release editor, port 8080): build box →
  transform_from_node → output; bind mesh; `set_node_transform` the driver;
  `get_geometry_graph` as the evaluation barrier; screenshot before/after —
  the graph mesh must follow. Then drag the driver with the gizmo live.
- Undo/redo of the drop and of a driver rename.
- Save/load round trip: `scripts/geometry_nodes_smoke_test.py` and
  `scripts/scene_roundtrip_verify.py`; verify an UNRESOLVED reference
  survives a save (no silent loss), and behavior when the driver was deleted
  before save.
- Duplicate-name scene: confirm deterministic resolution + debug log.

## Sources (code, all verified 2026-08-11)

- `src/editor/geometry_graph/nodes/lattice_node.cpp:58-121, 244-278,
  344-381` — the complete precedent.
- `src/editor/geometry_graph/geometry_graph_window.cpp:583-847` —
  update_live_nodes / snapshot / evaluation scheduling.
- `src/editor/assets/asset_reference.cpp:131-172`,
  `asset_manager.cpp:476-517` — resolution semantics.
- `src/editor/windows/item_reference.{hpp,cpp}`,
  `windows/item_tree_window.cpp:405-425` — widget + drag payload contract.
- `src/editor/graph_editor/graph_editor_node.cpp:178-294` — imgui hosting,
  content_scale, parameter-undo commit.
- `src/editor/parsers/gltf_extensions_export.cpp:466-475, 644-663`,
  `doc/gltf_extensions/ERHE_node_graphs.md` — why references persist by
  name, not index.
- `src/erhe/scene/erhe_scene/node.cpp:341-367` — the transform-update
  funnel + serials.
- `src/editor/mcp/mcp_server_graphs.cpp:333-352`,
  `mcp_server_tool_list.cpp:1271, 1302-1309` — MCP parameter path.
- `doc/geometry-graph-attribute-projection{,-handoff}.md` — the generic
  add-a-node recipe (this doc adds the missing MCP-enum step).
