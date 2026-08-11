# Handoff: AI creation tools / geometry graph — continuing work

Written 2026-08-11 at the end of a long session; read this FIRST in a
fresh context, then the canonical sources it points at. Everything from
today is committed on `main`, ALL UNPUSHED (the user pushes).
`git log --oneline -20` is the authoritative history; highlights below.

## Read these before working (canonical sources, in order)

1. `.agents/skills/erhe-creations/SKILL.md` — the creation workflow
   contract + gotcha index. MANDATORY before any creation work; it has a
   maintenance contract (fold new learnings in at session end).
2. `.agents/skills/erhe-creations/references/geometry_graph_sculpt.md` —
   ALL geometry-graph body-building recipes: roundness rule,
   exact-landing limb alignment + joint balls, pose rig via
   transform_from_node, union texturing, probed accents.
3. `doc/geometry-graph-transform-from-node.md` — design + implementation
   record of the transform_from_node node (the session's main editor
   feature); its "open follow-ups" list is the near-term backlog.
4. Memory: `project_20_frog_creation`, `project_transform_from_node`,
   `project_19_dolphin_creation` (auto-recalled; they point here).

## What exists now (landed today, main, unpushed)

- **Creation 20 frog** (`scripts/creations/creation_20_frog.py`), three
  passes: v1 basic (42c9343e), v2 detail — 25 graph parts, articulated
  legs, toes, probed iris/nostril/tympanum, fbm-mottle skin, CSG pad
  slits, dragonfly (6f5c3852), weld fix — segments overlap anchors +
  joint balls (88d193bb, user-reported gap bug), v3 LIVE POSE RIG
  (e5cb76f3): all 30 part poses are `transform_from_node` graph nodes
  driven by flat empty scene nodes under `Frog > Frog Rig`; move a
  driver, the part re-poses. Scene: res/editor/scenes/creations/frog.glb.
- **`transform_from_node` geometry-graph node** (7d6b72cd): applies a
  referenced scene node's transform (local/world) to input geometry;
  drag-and-drop driver assignment via `item_reference_imgui` on the
  canvas; name-keyed persistence; live tracking via `update_live()`;
  MCP-drivable (`geometry_graph_add_node` type enum extended — a step
  the older add-a-node docs omit). Verified 7/7 over MCP incl. scene
  save/load round trip. Follows `Lattice_node`'s driver pattern exactly.
- **Graph-hover -> Hierarchy highlight** (73031465 + 6d40614e): new
  transient `Item_flags` bits 34-36 (`hovered_in_graph`,
  `child_hovered_in_graph`, `ancestor_hovered_in_graph`; NOT in the
  glTF persistent allowlist). Hovering a graph node that references a
  scene node (Transform_from_node / Lattice_node, via new virtual
  `Geometry_graph_node::get_referenced_scene_node()`) flags that node +
  ancestors + descendants; the Hierarchy draws the same blue rect as
  viewport hover (folded ancestors take over via child_hovered).
  Maintenance: `Geometry_graph_window::update_graph_hover_flags()` —
  primary window from `update_evaluation()`, extra "[N]" windows ticked
  from `Editor_windows::update_once_per_frame()`.
  **VERIFICATION PENDING**: builds clean, logic mirrors Hover_tool, but
  the interactive hover check was handed to the user (automated
  cursor-over-canvas verification fails: console focus-steal clears
  hover, 125% display scaling skews coordinates — see Environment).
  ASK THE USER whether the highlight worked before building on it.

## Open follow-ups (rough priority)

1. Confirm the hover highlight with the user; fix if broken.
2. `project_attribute` node — queued NEXT TASK from earlier
   (memory `project_attribute_projection_node`): read
   doc/geometry-graph-attribute-projection.md + its -handoff.md first.
   Would give graph bodies proper per-axis UV control (the mottle-only
   texturing constraint traces back to this).
3. uid-in-key persistence for scene-node references: name-only keys
   break on rename and are ambiguous under duplicates; write
   `key.uid` (glTF uid) too and prefer it on load; upgrade
   `Lattice_node` and `Transform_from_node` together.
4. mat4 pin plumbing: `mat4_value` pin key exists with ZERO users;
   a mat4 output on transform_from_node + mat4 input on Transform_node
   would enable transform composition — and with it FK chains
   (today the rig is FLAT because a transform_from_node captures ONE
   node's local transform; nested drivers do not cascade).
5. Refactor `Lattice_node`'s hand-rolled drop target to
   `item_reference_imgui` (transform_from_node already uses it).
6. Optional: hovering the graph's Output node could highlight the
   BOUND scene node(s) via the `apply_baked_products_to_attachments`
   sweep (0..N nodes; deliberately skipped in the first cut).
7. The Laplacian `smooth` MCP op is BROKEN (explodes meshes) — known,
   unfiled, do not use; fix only if asked.

## Environment gotchas (this machine, cost real time today)

- Python: `python`/`py` on PATH are broken Store stubs in these shells;
  run `C:\Users\tksuo\AppData\Local\Python\pythoncore-3.14-64\python.exe`.
- Builds: `cmake --build build_vs2026_vulkan --config Release --target
  editor`. The exe cannot link while an editor runs (user may be USING
  it — check before killing; the user was live at the machine today).
- Screenshots: back up + set `edge_lines:false` in
  `config/editor/default_viewport_config.json` (backup kept at
  `%TEMP%\erhe_default_viewport_config_backup.json`), RESTORE after.
  The cursor-hover hotbar/label pollutes captures and lingers a few
  seconds after the mouse stops; `screenshot()` runs the hide pass that
  re-hides windows shown via `set_window_visibility` (use a raw
  `capture_screenshot` mutate instead when windows must stay up; the
  window arg is `title`, e.g. "Scene Hierarchy [1]").
- Repeated `--reuse` scene cycles can spawn the new viewport as a tiny
  corner window; relaunch the editor to restore the docked layout.
- Display scale is 125%: swapchain pixels = 1.25 x logical cursor
  coords (`GetClientRect` gave 1843x960 for a 2304x1200 swapchain).
- Frog iteration: `--reuse` full rebuild (~10 s, 594 MCP calls);
  `--only` unsupported (graph assets can't be recreated by name).

## State at handoff

Editor: windowed Release running, `frog` scene loaded (from glb),
Scene Hierarchy + Geometry Graph windows visible, Frog Graph targeted —
set up for the user's hover verification. Working tree clean except the
user's own config noise (desktop_windows.json, editor_settings.json —
do not touch) and long-standing untracked dirs. prompt_queue.txt and
`mcp-creation-scripts-*` memory only POINT at git log + the skill — do
not grow ledgers there.
