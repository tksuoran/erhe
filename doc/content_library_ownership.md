# Content library ownership

Stability: stable

Which scene a content-library resource belongs to, and how any consumer asks.
This is the design record; the library reference is
`doc/editor_content_library.md`, and where a resource sits in the scene's prim
tree is `doc/usd_compatibility_design.md` C5 / U4.

## The invariant

> Every `Content_library` has exactly one owner (a `Scene_root`). Every
> library resource is a member of exactly one `Content_library`, and reports
> that library's owner as its `Item_host`.

A `Scene_root` constructs its library and gives it the scene root node
(`set_owner(this, root_node)`). A library with no owning scene - the
`Scene_builder` template palette, the tool scene, the material preview scene -
keeps its own detached root scope and hosts its prims itself, so one hook
maintains the index in both cases. `erhe::Item_base` carries the host pointer
(`set_item_host`), which is not copied on copy or clone, and the default
`get_item_host()` returns it; the `Node` / attachment / `Scene` overrides are
unaffected.

There is no cross-scene aliasing of resources. A new scene is seeded with
copies (`copy_content_library`); a `Brush` copy shares the expensive payload
through `Brush::make_shared_payload_copy()`, so only the wrapper is
duplicated and GPU buffers never are. A resource moves between libraries by
copy: `copy_library_item_to_library` (collision suffix " (N)"), exposed as the
`copy_library_item` MCP tool and the "Copy to Scene" context menu.

A resource the scene RENDERS but does not own - a prefab template's material,
or one a mesh brought with it - is listed by no library at all; the mesh
binding gives it a slot in the scene's `Material_set` through that set's own
per-object membership. There are no reference entries and nothing to keep
operation-consistent.

## Requirements

- R1 Any library resource resolves its owning scene (or "editor-global")
  deterministically, with no active-scene guessing.
- R2 In-session identity matches on-disk identity: two scenes that each have
  their own copy after a reload have their own copy in-session too.
- R3 Scene files stay self-contained - a saved scene opens correctly on
  another machine with no side files (`doc/scene_serialization.md`).
- R4 Material edits stay scene-local: each scene gets fresh default materials
  precisely so edits do not leak across scenes.
- R5 No per-frame cost regressions, and brush payload duplication stays off
  the table.
- R6 Per-scene selection, the MCP tools, the Properties window and the graph
  systems all resolve the same answer to "which scene does this resource
  belong to".
- R7 The tool scene and the material preview scene keep working: they own
  private libraries and are not registered scenes.

## Host resolution in the consumers

- A resource prim in a scene's tree answers `get_item_host()` with that
  scene's `Scene_root`, which is what every "which scene is this in" consumer
  below asks. For a manager-owned asset type the further question "which
  container DEFINES it" is a manager lookup, not a host comparison:
  `Scene_commands::get_scene_root(Material*)` asks
  `Asset_manager::get_defining_scene_root`, and falls back to the active scene
  only for a material with no defining record - one not yet inserted, a loaded
  container's asset, a shared prefab template resource
  (`doc/asset_manager.md`).
- The Properties window's material texture combo resolves the library through
  the material's host, so it cannot offer another scene's textures.
- Per-scene selection puts resources in their host's `hosted_selection`
  bucket, so scoped clear and command-target semantics are uniform and MCP
  `get_selection` reports a scene for them. Ctrl-A in a viewport stays scene
  content only: resources are hosted for RESOLUTION, and the library trees
  stay their selection entry point.
- Texture and geometry graph output nodes resolve through the owning graph
  asset's host, so a graph's material output works with more than one scene
  open.

`App_scenes::get_single_scene_root()` remains for the genuine "which scene
does the user mean" cases, where no hosted item is in hand.

## Rejected alternatives

Each was rejected for a reason that still holds:

- **One editor-global library.** Scene files carry their own materials and
  textures (R3), so opening two saved scenes would merge their libraries -
  name collisions, no answer to "which Red belongs to which scene", and no
  good answer to what a scene close deletes. It also breaks R4, and makes
  selection and host reporting less informative rather than more.
- **A global library for brushes only, scene-owned for the rest, with a
  "make shared" promotion.** Brushes persist per scene (`ERHE_brushes`) and
  must (R3), so a global brush library needs merge and dedup on every scene
  load, with user-visible edge cases; loaded-scene brushes would leak into
  every other scene's palette; and promoting mutable resources reintroduces
  every problem of the global library for exactly those resources. The useful
  residue - an explicit copy-to-scene action - exists without a global
  library.
- **Libraries as external glTF asset files.** That breaks R3 unless every
  referenced resource is embedded too, at which point the reference is
  provenance rather than sharing, and it does not by itself answer R1. Real
  cross-file sharing is what the asset manager does
  (`doc/asset_manager.md`), and it layers on top of per-scene ownership
  because ownership makes every imported resource unambiguously owned by the
  importing scene.
- **Hosting the library wrapper instead of the resource, keeping aliasing.**
  An aliased resource is in N wrappers, so resource-to-host stays ambiguous -
  the original defect, one level removed - and selection selects resources,
  not wrappers. Any option that keeps aliasing keeps the defect.

## Known cost

A brush whose geometry is still a lazy generator when it is copied copies the
generator, so each scene's copy materializes its own geometry and primitive on
first use instead of sharing a later materialization of the template's. It is
one-time per brush per scene, never per frame; a brush materialized before the
copy shares its payload as designed.

## Verification

- `erhe_item_tests` covers the `Item_base` host pointer.
- Headless MCP, with two scenes open: create a material in scene B while scene
  A is active and `get_selection` reports it under scene B; a texture graph
  material output works with both scenes open; a save and reload of both
  scenes leaves the library diff clean; `copy_library_item` from A to B,
  edited in B, leaves A unchanged and round-trips.
- Interactive: cross-library drag-and-drop copies; the default scene and a new
  scene both have populated brush palettes, and brush placement works in both.
