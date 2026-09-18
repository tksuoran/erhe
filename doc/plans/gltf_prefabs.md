# glTF scene prefabs: outstanding work

Status: in progress

This plan extends `doc/editor/scene_serialization.md` (prefab instances save as glTF
2.1 `externalAssets` references), `doc/erhe/gltf.md` (what `erhe::gltf`
surfaces of glTF 2.1) and `doc/erhe/usd_compatibility_design.md` X2 (the structure
rule and the reference layer every instance obeys) with the prefab work that
is not built.

A glTF file can already be instantiated, several times, inside another scene;
instances stay live references to their source file; a scene holding instances
exports as glTF that references the sub-scenes as external assets; a glTF
instance is sealed and is edited by opening its source as a scene and saving
it back, which reloads the prefab and refreshes every instance in every scene.
What follows is what that does not cover yet.

## Create prefab from selection

Export the selection to a `.glb` through the existing exporter, then replace
the selection with an instance of it, as one undoable compound operation.

## Skin and animation remapping

`Node::clone()` does not remap the node pointers a `Skin` and an animation
channel hold, so a cloned skinned mesh would deform against the TEMPLATE's
joints. Instantiation therefore warns and keeps skinned prefabs static
(`prefab_library.cpp`).

The fix is a source-to-clone node map built during instantiation - walk the
template and clone trees in lockstep, or extend the clone framework with an
optional remap context - and then remap `Skin` joint pointers and animation
channel targets. That makes animated prefabs (characters, for instance) work
with independent playback.

## Physics inside prefabs

`import_gltf` builds physics through separate operations rather than through
node attachments that `Node::clone()` copies, so a prefab instance carries no
physics. Instantiating an `erhe::scene::Physics_description` per instance
needs the same node remap the skins do.

## Embedded sub-assets

Support `files` entries with data URIs and GLB-packed payloads: parse them
from memory, with the cache key being the parent canonical path plus the file
index.

## Open questions

- **Content_library is per `Scene_root` while `Prefab_library` is app-wide.**
  Instantiating one prefab into two scenes registers the same shared materials
  and textures in both libraries. That is acceptable - textures are
  device-global and materials are `shared_ptr` - but "edit the material of an
  instance" then edits it everywhere, including the template. That is standard
  prefab semantics; a per-instance material is not expressible, while a
  per-instance property value is.
- **Spec gaps.** The glTF 2.1 explainers do not pin down externalAsset-node
  children or scene selection. erhe's interpretations (default scene, tolerant
  import, strict export) are recorded here so they can be revisited when the
  normative text lands; watch KhronosGroup/glTF#2586.
- **File watching.** A prefab reloads on an explicit `reload_prefab` or on a
  save of its source through the editor. Watching the source file on disk and
  reloading when another tool writes it is not implemented.
- **No render-level instancing.** Instances share GPU vertex and index data
  but still draw per mesh node. `EXT_mesh_gpu_instancing` for many-instance
  scenes is a separate optimization.
