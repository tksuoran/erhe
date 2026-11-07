# Source, editor and prepared assets: requirements

Status: proposed

Requirements for splitting a scene into a source asset, an editor asset and
prepared assets. This document holds requirements only; the design and the build steps
are in `doc/plans/prepared_assets_design.md`. It extends
`doc/editor/scene_serialization.md` (Prepare) and takes over the subject of
`doc/plans/prepare_pvs_export.md`.

## Terms

- Source asset: the glTF file a scene is authored in and saved to
  (`doc/editor/scene_serialization.md`), either erhe-authored (`ERHE_scene`)
  or a foreign glTF.
- Editor asset: the store of all data derived from one source asset. It
  speeds up the editor (anything the editor would otherwise recompute at
  load or during work) and it holds every item a prepared asset of any
  target is made of.
- Prepared asset: the final form of one scene for one target, which the
  editor on a target device (for example a Quest) runs the scene from when
  the source asset is not there (R11). It is a stripped copy of editor
  assets (R9.3).
- Target: the device family a prepared asset is made for: `pc` or `quest`.
- Item: one piece of derived data in an editor asset or a prepared asset,
  made from source content, settings and other items.
- Key: the hash of everything an item's content depends on (R3.2). An item
  is stored and found by its key.
- Drawn mesh: the one mesh item a mesh node of a target's Scene item
  refers to: the World-space mesh item when the node is admitted by the
  world-space bake (R4.6), the Local-space mesh item otherwise.
- Export settings: the scene's `Scene_export_settings` (R7).
- Export: the action that makes a prepared asset for a target (R9); what
  File > Export (PC) / Export (Quest) and MCP `prepare_scene` run.
- Prepared run: the editor running a scene from a prepared asset (R11).

## R1 Three assets

- R1.1 The source asset holds authoring data. Editor assets and prepared
  assets hold derived data only: everything in them can be made again from
  source assets and export settings.
- R1.2 An editor asset belongs to one source file and holds items for
  every target side by side. A prepared asset belongs to one scene and one
  target.
- R1.3 Each item of a prepared asset is a copy of an item of an editor
  asset, identical in content and key.
- R1.4 A prepared asset is complete for running: the scene opens from it
  alone, with every source asset and editor asset absent.
- R1.5 erhe is one program on every platform: the editor on a PC and the
  editor on a Quest follow the same rules.
- R1.6 Editor assets and prepared assets are build products: deleting one
  loses no work, and the repository's ignore rules cover `*.erhe_editor`
  and `*.erhe_prepared`.
- R1.7 This work covers scenes whose source asset is a glTF file. A
  USD-backed scene (`doc/editor/scene_serialization.md` "USD-backed
  scenes") has no editor asset, and Export and the bakes of R8 refuse it
  with a message.

## R2 Links and locations

- R2.1 The editor asset and each prepared asset record their source asset:
  a relative path and the identity of the source scene (the `scene_id` of
  `ERHE_scene` when the source has one).
- R2.2 The source asset records its editor asset by a path relative to
  itself. An erhe-authored source carries it in `ERHE_scene`, written by
  Save Scene.
- R2.3 The editor asset records, per target, the path of the prepared
  asset Export last wrote.
- R2.4 The default location of the editor asset is
  `<source stem>.erhe_editor` beside the source file: the editor looks for
  and creates it there when the source names none. The default location of
  a prepared asset is `<source stem>.<target>.erhe_prepared` beside the
  source file: Export writes it there when no path is given.
- R2.5 When the default location of an editor asset cannot be written (a
  read-only asset pack), the editor asset is created in a fallback
  location under the editor's writable cache directory, named from the
  source file's path, and used the same way; the editor logs the fallback
  once per file. When the fallback cannot be written either, the editor
  logs an error naming the file and both locations and works without
  stored items for that file.
- R2.6 The links of R2.1 - R2.3 survive moving the files together to
  another directory or machine. An editor asset in a fallback location
  (R2.5) stays behind and is made again.
- R2.7 A scene that has no source file yet (created in the editor and not
  saved) has no editor asset: the editor derives what it needs in memory,
  and the first Save creates the editor asset beside the file.
- R2.8 An editor asset is used whichever source file its record (R2.1)
  names: its items are found by key.

## R3 Items and keys

- R3.1 The store is content addressed: an item is stored and found by its
  key, and holds its key with it in every asset.
- R3.2 A key is the hash of everything the item's content depends on:
  - the content hashes of the source content it is made from (R3.4);
  - the export settings that affect that kind of item;
  - the keys of the items it is made from;
  - a version number of the code that makes that kind of item;
  - the format stamp of the kind (R3.9).
- R3.3 The editor computes the key of each thing a scene asks for and
  looks it up. An item is referenced when an open scene, under its current
  settings, asks for its key, and unreferenced otherwise. An edit changes
  which keys are asked for and changes no stored item: one edited texture
  asks for a new Texture key and leaves every item in place, and undoing
  the edit, or setting a value back, asks for the earlier key again and
  finds its item. Unreferenced items are kept; they leave an editor asset
  only through R8.13 or R5.2.
- R3.4 Content hashes are 128 bits or wider (XXH3-128 is the intended
  function) and cover the whole content: every attribute and the indices
  of a mesh, every byte of an image.
- R3.5 A mesh primitive and an image carry their content hash as loaded
  state: it is computed on the worker that loads the content, while the
  bytes are in memory, and an edit that changes the content computes it
  again at the edit. Computing keys combines these stored hashes with
  settings and reads no file.
- R3.6 Keys are computed from the open scene as it is, so an unsaved edit
  changes the keys as a saved one does and Export exports what is on
  screen.
- R3.7 An edit that changes source content, a node's placement or an
  export setting marks the scene's status stale at the edit (change-driven,
  `AGENTS.md` 'No "update each frame" patterns'); the keys are computed
  again when an update is requested (R8.12; 2026-11-07, user decision).
- R3.8 Computing all keys of a scene costs far less than making the items
  (target: Bistro in under 2 seconds on the desktop). Key computation,
  lookup and storing run in the background: the scene is usable while they
  run, and the editor's frame stays free of the work.
- R3.9 Every item kind has a format stamp: what the stored bytes require
  of the build that reads them (the BVH library version for a Raytrace
  BVH; the vertex layout signature for a mesh item; the block format for a
  Texture). The stamp is part of the key, so a build with a different
  stamp asks for different items. One mesh item serves both targets while
  their builds share a vertex layout.
- R3.10 A kind whose content differs per target has one item per target; a
  kind whose content is the same everywhere has one item the targets
  share.
- R3.11 A new kind of item (a thumbnail) is added without changing how
  existing kinds are stored or found, by stating its inputs, its content
  and what refers to it.

## R4 Item kinds

- R4.1 The kinds:

| Kind | Made from | Content | Referred to by |
|---|---|---|---|
| Texture | one image, its texture properties (R4.2), the target | The image in the target's block format (ASTC 4x4 for `quest`; BC7 / BC5 for `pc`) with mip levels, or the source image bytes (R7.5) | a material of a Scene item of that target |
| Basis texture | one image, its texture properties | The image as Basis Universal KTX2, the target-independent step the Texture items of R7.5 `target` are transcoded from | nothing: editor asset only |
| Local-space mesh | one mesh primitive (R4.4), the LOD request | Vertices in the primitive's own space: final vertex format (GPU-ready vertex streams), optimized index and vertex order, bounds, LOD chain | a mesh node of a Scene item whose drawn mesh it is |
| World-space mesh | one Local-space mesh item, one world transform (R4.6) | The same content with vertices baked to world space and the LOD chain generated in world space | a mesh node of a Scene item whose drawn mesh it is |
| Raytrace BVH | one mesh item of either kind | Bounding volume hierarchy for picking and ray queries | its mesh item |
| Scene | the scene, the target | The scene as a prepared run loads it (R4.9) | the prepared asset itself: the references start from it |
| PVS | the scene (R4.10) | Potentially visible set database (`doc/editor/pvs.md`) | a Scene item, per `pvs_mode` (R7.6) |
| Editor data | any source content | Data only the editor uses: mesh topology and edge data for editing, the mesh optimizer's results, anything else the editor caches to open and work faster | nothing: editor asset only |

- R4.2 A source texture carries the properties its conversion needs, as
  authoring data of the texture, saved in the source asset and shown in
  its Properties:
  - transfer function (sRGB or linear);
  - normal encoding (not a normal map, three-component, two-component);
  - mipmap generation: on or off, and the wrap mode in u and in v the
    filter uses at the image's edges (repeat, clamp, mirror);
  - further conversion parameters are added the same way.
- R4.3 Texture conversion reads the texture properties and consults no
  material. A texture that states none (a foreign glTF) takes them, once,
  at import, from the material slots and the sampler that use it; an image
  used in conflicting ways takes the first use and the import logs the
  others.
- R4.4 Every mesh primitive has a Local-space mesh item, whatever its
  source of truth: for an imported triangle soup it is made from the soup;
  for a geometry-normative primitive (`ERHE_geometry`) and for a mesh a
  geometry graph produces, it is made from the triangles the editor
  derives from the geometry. Such a mesh takes part in the world-space
  bake, LOD generation and the BVH like any other.
- R4.5 The per-primitive LOD request is authoring data: it stays in the
  source asset in `ERHE_mesh_lod`, which carries the request alone. The
  generated chain is part of the mesh item.
- R4.6 The world-space bake admits the mesh nodes
  `doc/editor/world_space_bake.md` "Which meshes are baked" admits, with
  two changes: geometry-normative and graph meshes are admitted (R4.4),
  and so are the static meshes inside prefab instances (R10.4). The
  `bake_static_to_world_space` and `instancing` export settings apply; the
  default of `instancing` is `duplicate_within_budget`.
- R4.7 A World-space mesh item's key covers its Local-space mesh item's
  key, the world transform as its exact bits, and the settings it is baked
  with. It is found by that key alone: no lookup depends on which node
  asks, and two nodes placing one mesh at one transform share an item.
- R4.8 Beside its key, a World-space mesh item records every node it was
  last made or looked up for, as information: the node's path of names
  from the scene root and its chain of `uid`s (R6.2). The status of R13.1
  and the logs use it to name the node an item belongs to.
- R4.9 The Scene item is a glTF document, JSON and binary data the way
  glTF holds them, stored as one item and written and read by the glTF
  exporter and loader the source asset uses. It holds the node tree as the
  target sees it (the world-space bake applied, every prefab instance
  expanded, every referenced material resolved), materials, lights,
  cameras, physics, scene settings and the `ERHE_*` state a run needs, and
  skins and animations with their binary data. It refers to the Texture,
  mesh, Raytrace BVH and PVS items by key in place of carrying their
  payloads. It holds no brushes, no polygon geometry and no geometry
  graphs.
- R4.10 The PVS is the one kind outside R3.2 - R3.3: an editor asset holds
  at most one PVS item per scene, what was baked last, and the `pvs_mode`
  export setting (R7.6) states what Export does with it. It names its
  columns by the nodes of the Scene item.
- R4.11 Baked lightmaps (`doc/editor/lightmap_baking.md`) are outside this
  work and stay as they are.

## R5 Container

- R5.1 The editor asset and the prepared asset use the same container
  format: one file each at rest, supporting adding, replacing and removing
  single items without rewriting the rest. The intended container is an
  SQLite database; SQLite becomes a dependency of erhe, built from source
  on every platform erhe builds on, Android included. A prepared asset on
  a device is a regular file in the app's storage, opened read-only.
- R5.2 The container carries a format version. The editor recreates an
  editor asset whose format version it does not read; opening a prepared
  asset whose format version the build does not read fails with a logged
  error.
- R5.3 Writes are transactional: a crash, a cancel or a power loss leaves
  an asset in which every stored item is whole; a partly written item is
  never found by a lookup.
- R5.4 A reader can read one item, or a byte range of one item, without
  reading the others, so the editor streams mesh and texture payloads as
  it does from a prepared glTF file today (`doc/plans/gltf_fast_load.md`
  item 9).
- R5.5 Payloads are stored in the form the runtime consumes, with the
  supercompression the runtime already reads (Zstandard KTX2 levels).
- R5.6 Several readers and one writer at a time can have an editor asset
  open, across threads and across editor processes (two scenes sharing a
  prefab's editor asset; two editor instances). A writer that cannot get
  its turn within a bounded wait logs it and goes on without storing.

## R6 Stable identities and transforms

Two changes to existing behaviour that the items rely on.

- R6.1 A Save writes every node's local transform as the node holds it and
  composes no transform into another node's, so every world transform, and
  with it every World-space mesh key, is the same before a Save and after
  a reopen. For the implicit root node an open or import of a glTF creates
  (`Item_flags::import_root`):
  - while it carries `Item_flags::world_space` (its path from the scene
    root is the identity) it is written as no node and its children are
    written in its place;
  - once it is transformed it is scene content: the save writes it as a
    node with its transform, and its children below it unchanged.
  The flag is derived: a transform edit on the root clears it at the edit,
  and setting the identity back restores it.
- R6.2 Every node, and each other object that carries a glTF `uid`, has
  its `uid` from the moment it exists, the same in every session for the
  same content:
  - an object of a glTF that states none takes one derived from its kind
    and its index in the source file, at import;
  - a node inside a prefab instance takes one derived from the `uid` of
    the instance's carrier node and the `uid` of the template node, at
    instantiation, so nested instances chain;
  - an object created in the editor takes a random one at creation, which
    the next Save persists.
  A derived `uid` that collides with another identifier of the file is
  resolved by a rule that gives the same result every time. Export and the
  PVS bake find every `uid` present and assign none, so the Scene item of
  an unchanged scene is the same in every session.

## R7 Export settings

- R7.1 The export settings are per scene, in `Scene_export_settings`,
  saved in the source asset. A scene has them or does not. A new or
  imported scene has none. Adding them (the scene's Properties "Export"
  group, MCP `set_scene_settings`) copies the editor's default export
  settings into the scene; from then on every field holds a concrete
  value, the LOD request included.
- R7.2 The scene's export settings are the only settings input of every
  item: the editor's configuration is read at the copy of R7.1 alone, and
  a call (menu or MCP) carries no settings of its own. MCP changes a
  setting by writing the scene's export settings before the call. The
  items of a scene are therefore the same on every machine.
- R7.3 Export and the bakes of R8 fail with a message saying what to do
  for a scene without export settings (add them) and for a scene without a
  source file (save it first, R2.7).
- R7.4 The settings, beside the ones `doc/editor/world_space_bake.md`
  "Export settings" lists: `texture_encoding` (R7.5),
  `texture_supercompression`, `pvs_mode` (R7.6) and `raytrace_bvh` (Bool,
  default true: a prepared asset carries the Raytrace BVH items).
- R7.5 `texture_encoding` states what a Texture item holds for a source
  image that is neither block compressed nor Basis Universal (a PNG, a
  JPEG):
  - `target` (the default): the image encoded to the target's block
    format, by way of its Basis texture item (R4.1);
  - `source`: the source image bytes as they are; the run decodes them at
    load.
  A Basis Universal source image is transcoded to the target's block
  format under either value.
- R7.6 `pvs_mode` states what Export does about the PVS:
  - `off` (the default): the prepared asset holds no PVS;
  - `reuse` (unsafe): Export copies the PVS item the editor asset holds as
    it is, with no check that it matches the exported geometry; the
    setting's description and the Export log say so. With no PVS item,
    Export writes none and logs it;
  - `rebake` (safe): Export bakes a new PVS (R8.11) every time. With the
    PVS region unset (`pvs_region_min` and `pvs_region_max` both zero)
    Export fails before making anything, with a message asking for a
    region or another mode.

## R8 The editor and the editor asset

- R8.1 When the editor opens a source asset it finds the editor asset
  (R2.2, then R2.4, then R2.5) and opens it, creating an empty one when
  none exists, without asking the user. It then computes the keys the
  scene asks for and finds which items are present and which are missing.
- R8.2 The editor loads a present item in place of deriving the same data
  from the source asset, so a scene with a filled editor asset opens
  faster than one without.
- R8.3 Whenever the editor derives data an item kind covers and no present
  item holds it (a BVH built for picking, an optimized mesh at load, a LOD
  chain), it stores the result as an item, so the work is done once. The
  items the editor has no use for itself (the other target's textures,
  world-space meshes, a PVS, a Scene item) are made by Export and the
  bakes below alone.
- R8.4 The editor asset takes over three stores of derived data: the
  prepared mesh data Save Scene writes into the source glTF, and the
  per-content entries of `cache/bvh` and of `cache/mesh_optimizer`, become
  items of it.
- R8.5 An open scene holds two forms of each mesh node the world-space
  bake admits (R4.6): its source mesh, drawn through the node's transform,
  and its world-space mesh, the World-space mesh item of the node's
  current key. The node tree stays as authored; the "World Space" node
  exists in a Scene item alone.
- R8.6 A node's world-space mesh is valid while the item of its current
  key is present. Moving the node, editing its mesh or changing a setting
  the key covers makes it invalid at the edit (R3.7); it becomes valid
  again through R8.7 or by the edit being undone (R3.3).
- R8.7 Bake World-Space Meshes is an explicit operation (menu and MCP)
  that makes the missing World-space mesh items of the scene. It replaces
  the Bake Static To World Space operation that rearranges the live node
  tree.
- R8.8 A viewport toolbar setting chooses which form the viewport draws:
  source meshes or world-space meshes. World-space is offered while every
  admitted node's world-space mesh is valid; otherwise the viewport draws
  source meshes and the toolbar shows how many are missing.
- R8.9 The PVS bake (PVS window, MCP `pvs_bake`, and Export's `rebake`)
  runs R8.7 first and renders the world-space form of the open scene; the
  live node tree stays as it is and no second scene is loaded. Its result
  replaces the PVS item of the editor asset.
- R8.10 PVS culling applies in a viewport that draws the world-space form.
- R8.11 The editor reports the editor asset's path, its size, the size per
  item kind and target, and the count and size of unreferenced items.
- R8.12 The status of a scene (R13.1) is computed on request (a button
  where it is shown, an asynchronous MCP request) and shows whether a change
  came since (R3.7).
- R8.13 The user can drop the unreferenced items, all items of one target,
  or all items, from an editor asset; dropped items are made again on
  demand, and the file gives the space back.

## R9 Export

- R9.1 Export takes a scene and a target and runs two steps: Ensure, then
  Copy.
- R9.2 Ensure makes every missing item the target's Scene item will refer
  to, in dependency order: Basis texture, Texture and Local-space mesh;
  then World-space mesh (R8.7); then Raytrace BVH; then the Scene item;
  then the PVS when `pvs_mode` is `rebake` (R8.9). Present items are used
  as they are.
- R9.3 Copy brings the target's prepared asset to hold exactly the items
  its Scene item refers to, directly or through another item: the Scene
  item; per mesh node its drawn mesh and, with `raytrace_bvh` on, that
  mesh's Raytrace BVH; the Texture items its materials use; and the PVS
  per `pvs_mode`. A baked node thus brings its World-space mesh alone, and
  a Local-space mesh is copied only when some node draws it. Copy writes
  the prepared asset anew from the editor assets, to a temporary file
  renamed over the target when complete; it reuses nothing of a previous
  prepared asset. A key an editor asset lacks fails the Export, naming the
  key and its kind, and leaves the previous prepared asset untouched. A
  prepared asset is complete by construction and read-only once written; a
  prepared run validates nothing about its content (R11.3) and reads only
  the file's compatibility with the build (R11.4, R11.6). Rationale: a
  fresh write from the editor assets, which Ensure has just made complete,
  leaves no earlier state to reconcile or verify, and the rename makes the
  replacement all or nothing.
- R9.4 Export of an unchanged scene with unchanged settings and a
  `pvs_mode` of `off` or `reuse` makes no item in the editor assets and
  reports so; Copy writes the same prepared asset again (R9.3).
- R9.5 Export leaves the open scene and the source asset as they were,
  whether it succeeds, fails or is cancelled: the node tree, the dirty
  state and the undo stack are the same after it as before.
- R9.6 Export holds the editor read-only from its start to its end, the
  PVS bake included, under the rules of
  `doc/editor/scene_serialization.md` "Saving in the background": the
  scenes take no edits, operations are held, and the windows' contents
  are disabled. Two things stay live: the viewports keep drawing, and the
  editor's own viewport camera (the one that is not part of the exported
  scene) can be moved to inspect the scene. The PVS bake steps run during
  Export.
- R9.7 Export reports its step, stage and progress in the status line and
  through MCP, and can be cancelled; items finished before a cancel stay
  in the editor asset, and a cancelled Copy leaves the prepared asset as
  R5.3 states.

## R10 Scenes built from several files

A scene's content can come from more than one glTF file: the scene file,
the prefab sources it instantiates as glTF 2.1 `externalAssets` (nested to
any depth; cycles are rejected at load), and the container files its
referenced materials are defined in (`ERHE_asset_reference`,
`doc/editor/asset_manager.md`).

- R10.1 Every source file has its own editor asset (R2), whether it is
  opened as a scene, instantiated as a prefab or read as a material
  container.
- R10.2 An item is stored in the editor asset of the file that defines its
  source content when all of its inputs come from that one file (Basis
  texture, Texture, Local-space mesh and its Raytrace BVH, Editor data): a
  prefab used by ten scenes has its meshes and textures made once. An item
  whose inputs include where and how the content is used (World-space mesh
  and its Raytrace BVH, Scene, PVS) is stored in the editor asset of the
  scene being worked on.
- R10.3 A prepared asset is flat: Copy (R9.3) gathers its items from every
  editor asset involved, and a prepared run resolves no external file.
- R10.4 The world-space bake covers the static meshes inside prefab
  instances; the `instancing` export setting decides whether each instance
  gets its own World-space mesh or the instances keep sharing one
  Local-space mesh through their node transforms.
- R10.5 Content that comes from a prefab is prepared with the export
  settings of the host scene being exported. Where the prefab source has
  export settings of its own and a value that affects the prefab's items
  differs from the host's, the host's value applies and Export logs the
  setting, the prefab and both values. A prefab's editor asset can so hold
  several variants of one item, one per distinct setting among the scenes
  that use it; the variants the open scene does not ask for are
  unreferenced from its view and are kept (R3.3).
- R10.6 A reload of a prefab's instances (a Save of the prefab source,
  MCP `reload_prefab`) is the change site (R3.7) that marks the status of
  every open scene that instantiates it stale. A scene that was closed finds
  the change at its next open through R3.6.
- R10.7 Ensure (R9.2) makes the items of the files a scene depends on
  before the scene's own, deepest prefab first.
- R10.8 Export fails with an error naming the file when a prefab source or
  a material container of the scene cannot be read, or when no editor
  asset of a file can be written (R2.5); a prepared asset never holds a
  stub in place of missing content.
- R10.9 The Scene item is made from the open scene, so it carries what an
  instance carries there: a skinned prefab instance is static and an
  instance has no physics until `doc/plans/gltf_prefabs.md` lands those.

## R11 Opening a prepared asset

- R11.1 When the editor, on a PC or a Quest, is asked to open a prepared
  asset (File > Load Scene, `--scene`, the headset scene selection, MCP):
  - when it can locate and read the source asset the prepared asset
    records (R2.1), it opens the source asset and works with it as with
    any source asset (R8);
  - otherwise it runs the scene from the prepared asset, a prepared run,
    and logs a notice naming the prepared asset and the source it could
    not read.
- R11.2 In a prepared run the scene is edited live like any other
  (selection, transforms, operations, undo) and the edits last for the
  session: Save and Export are refused for the scene with a message saying
  why, and no editor asset is created for it. An edit that needs data a
  prepared asset does not hold (mesh topology for a geometry edit) is
  unavailable there.
- R11.3 A prepared run loads every item as stored and derives nothing at
  load: no transcode, mesh optimization, LOD generation, geometry build,
  graph evaluation, BVH build, PVS bake or key computation. The exceptions
  are R11.4 and R11.5. It validates nothing about the asset's content (an
  Export writes it complete, R9.3); a damaged asset fails at the parse.
- R11.4 A prepared asset records, at Export, the format stamps (R3.9) of
  the items it holds, one per kind (the Texture items may record several).
  A prepared run whose recorded stamp of a kind differs from its own
  build's logs the kind and both stamps; builds the Raytrace BVHs in memory
  from their meshes in place of the stored ones; and fails the open with an
  error for mesh or Texture items, which it cannot rebuild from what a
  prepared asset holds. The stamps are a compatibility check of the file
  with the running build, not a validation of the asset. The recorded
  stamps suffice: Export makes every item it puts in by its current key,
  the key includes the stamp, and nothing changes a prepared asset after
  its Export, so every item carries the exporting build's stamps and the
  open reads no item's own.
- R11.5 In a prepared run of an asset made with `raytrace_bvh` off, a tool
  that needs ray queries builds the BVHs it needs in memory and logs it.
- R11.6 A prepared run of an asset of another target is allowed where the
  build reads the stored formats (a PC running a `quest` asset decodes its
  ASTC textures), with the target named in the notice; where it does not,
  the open fails with an error naming the target.

## R12 What this work removes

Each removal happens once the requirements that replace it are verified
(R14.4), together with the conversion of the tests, scripts and device
recipes that use what is removed (`erhe_gltf_tests`, `mcp_server_tests`,
`scripts/lod_roundtrip_verify.py`, the `load.use_prepared_mesh_data`
setting, the glTF extension schemas, `doc/agents/quest.md`).

- R12.1 The prepared glTF save kind (`_pc.glb` / `_quest.glb`).
- R12.2 The glTF extensions that carry derived data alone:
  `ERHE_mesh_optimized`, `ERHE_mesh_topology`, `ERHE_gpu_vertex_streams`,
  `ERHE_raytrace_bvh`, `ERHE_texture_ktx2`, `ERHE_pvs`; and the chain data
  of `ERHE_mesh_lod` (R4.5). A source asset then carries authoring data
  alone (R1.1).
- R12.3 The `cache/bvh` and `cache/mesh_optimizer` directories.
- R12.4 The per-call `export_settings` merge of MCP `prepare_scene` and
  `bake_static_to_world_space`, and the `texture_supercompression`
  argument (R7.2).

## R13 Status and verification

- R13.1 The editor reports, per scene and target: the count of present and
  missing items per kind, counted over every file the scene depends on;
  how many world-space meshes are valid; whether the prepared asset
  matches what Export would write; and the paths. It does so in the
  scene's Properties and through an MCP tool.
- R13.2 Verified on Bistro, headless Release, export settings added and
  `instancing` `duplicate`:
  - the first Export makes every item; the prepared asset, opened as a
    prepared run, renders as the source scene within the bounds stated in
    `doc/editor/world_space_bake.md` "Verification";
  - a second Export, in a new session, makes zero items (R9.4);
  - after editing one texture, Export makes that texture's items and the
    Scene item alone; after moving one static mesh node, it makes that
    node's World-space mesh, its Raytrace BVH and the Scene item alone;
    undoing the move and exporting again makes nothing;
  - with `pvs_mode` `rebake` and the region of `doc/agents/quest.md`, the
    prepared run's PVS is `ready`;
  - the prepared asset holds no Editor data, no Basis texture, no item of
    the other target and no Local-space mesh of a baked node;
  - an Export killed midway, then run again, ends in a prepared asset
    with the same keys and the same bytes per item as an uninterrupted
    one;
  - saving the foreign Bistro glTF as an erhe scene and reopening it
    leaves every world-space mesh valid (R6.1).
- R13.3 Verified on Bistro for the editor's own loading: the second open
  of the scene builds no BVH and runs no mesh optimizer, and reaches idle
  sooner than the first.
- R13.4 Verified on Quest 3 with the Bistro recipe of
  `doc/agents/quest.md`: the prepared run of R11 loads and renders the
  scene; its load time and peak memory are recorded
  (`doc/reference/quest_bench_runs.md`) and compared with no other load
  (2026-11-07, user decision: the editor writes no prepared glTF file to
  compare with).
- R13.5 Unit tests without a GPU cover content hashing, key computation,
  lookup, dropping unreferenced items, the Copy step, the container's read
  and write paths with two writers, R6.1 and R6.2.

## R14 Build order

The steps are in `doc/plans/prepared_assets_design.md` (D7, D8). They keep
these constraints:

- R14.1 A minimum viable product comes first: Export of a simple scene
  into an editor asset and a prepared asset, and a prepared run of it,
  without Texture items, the PVS or world-space meshes.
- R14.2 The two changes to existing behaviour (R6) land, each verified on
  its own, before World-space mesh items.
- R14.3 The Export work (R7, R9, R10, R11) comes before the editor's own
  loading and storing (R8.1 - R8.4). Between the two, derived data lives
  in two places: the editor asset serves Export, and the editor's own
  loading uses the source glTF extensions and `cache/` as it does today.
- R14.4 Each removal of R12 follows the verification of what replaces it:
  R12.1 and R12.4 after R13.2 and R13.4, R12.2 and R12.3 after R13.3.

## Later

- Saving a scene edited in a prepared run (R11.2), and rebuilding on
  demand the data its unavailable edits need.
- Editor assets and Export for USD-backed scenes (R1.7).
- Baked lightmaps as an item kind (R4.11).
- An export setting that keeps polygon geometry and geometry graphs in a
  prepared asset (R4.9).
- The PVS as a key-tracked item, so `rebake` runs only when an input
  changed: its inputs are every entry of the bake's static set
  (`doc/editor/pvs.md` D3) with its mesh key, transform and blending
  class, the alpha textures of alpha-tested entries, the region, the cell
  size and the bake parameters.
- Export requiring a scene without unsaved changes (R3.6).
- An item flag that marks single meshes to keep or drop their Raytrace BVH
  in a prepared asset, refining `raytrace_bvh` (R7.4).
- Export for a target build whose format stamps differ from the exporting
  build's (R3.9).
