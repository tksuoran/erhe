# ERHE_scene

## Scope

**Scene** extension. Optional (`extensionsUsed` only).

## Overview

Marks a file as an erhe-authored scene and carries per-scene editor state.
Its presence in `extensionsUsed` is the discriminator between "open as a
full erhe scene" and "import as an asset": scene saves always write it,
plain interchange exports never do.

- `ambient_light`: scene ambient light color (RGBA; erhe #237).
- `enable_physics`: whether the scene owns a physics world.
- `settings` (optional): per-scene overrides of editor-global settings
  (erhe #239) as a `Scene_settings` JSON object (the erhe_codegen schema in
  `src/editor/scene/definitions/scene_settings.py`; each absent / null
  field means "use the editor-global default"). Omitted entirely when no
  override is engaged.
- `styles` (optional): the content library's style items
  (`doc/style-library.md` D4): `name` and `properties` (the style's local
  values as a name to text map, the form of `ERHE_node` `properties`,
  keyed by qualified name such as `Material.roughness` or `Light.color`;
  omitted when empty). A `target` member of older files is ignored. Loaded before anything that names a style. Omitted when the
  library has no styles.
- `physics_materials` (optional): one entry per `KHR_physics_rigid_bodies`
  `physicsMaterials` entry, by index (the KHR entries carry no name):
  `name` and `properties` (the material's local property values as a
  name to text map, the form of `ERHE_node` `properties`; the registered
  properties of `Physics_material` by name, so the erhe-only
  `linear_damping`, `angular_damping`, `wind_receptivity` and `density`
  have their carrier here). The map is the material's complete local
  set: on load, a value the KHR entry carried (friction, restitution, a
  combine mode) that the map does not name is cleared again, so a
  material that inherits it from its folder or style still does after a
  reload. Omitted when the file has no physics materials.
- `collision_filter_names` (optional): the names of the
  `KHR_physics_rigid_bodies` `collisionFilters` entries, by index, so the
  content library's collision filters keep their names across a save and
  reload. Omitted when the file has none.
- `library_folders` (optional): where the scene's content-library resources
  sit in the scene tree (`doc/content-library-folders.md` D5), parents before
  their children. Each entry has `path` (the prim's slash-separated path from
  the scene root, `Hierarchy::get_path()`), `properties` (the prim's local
  property values as a name to text map, the form of `ERHE_node`
  `properties`; omitted when empty), `items` (the names of the resources
  directly under the prim; omitted when empty) and `style` (the name of the
  style item the prim uses; omitted when none). A `path` whose first component
  names a resource kind's scope (`Materials`, `Brushes`, ...) is a scope path:
  the load creates the scopes it names. Any other `path` names a prim of the
  scene tree - an `Xform`, a `Mesh`, a `Scope` under one - and the load
  resolves it against the tree after the nodes exist, never creating one. A
  resource no entry names loads into its kind scope, which is why entries for
  a bare kind scope are not written. Omitted when every resource sits under
  its kind scope.

## JSON layout

```json
{
    "ambient_light": [0.1, 0.1, 0.12, 1],
    "enable_physics": true,
    "settings": {
        "post_processing": false,
        "clear_color": [0, 0, 0, 1]
    },
    "styles": [
        {"name": "Brushed metal", "properties": {"Material.roughness": "0.34 0.2", "Material.metallic": "1"}}
    ],
    "physics_materials": [
        {"name": "Rubber", "properties": {"restitution": "0.8", "linear_damping": "0.1"}}
    ],
    "collision_filter_names": ["Debris"],
    "library_folders": [
        {"path": "Materials/Metals", "properties": {"visible": "false"}, "items": ["Gold", "Copper"], "style": "Brushed metal"},
        {"path": "Brushes/Platonic Solids", "items": ["Cube", "Octahedron"]},
        {"path": "Panel/Looks", "items": ["Panel Paint"]},
        {"path": "Bound Box", "items": ["Box Paint"]}
    ]
}
```

## Load semantics

Consumed by the Open-Scene path when constructing the `Scene_root`.
Importing a file that carries `ERHE_scene` as an ASSET into an existing
scene ignores everything but `library_folders` - an import must not change
the target scene's settings, while the imported library entries keep their
folders. In both paths the folders are placed after every library entry of
the file exists; a folder that already exists keeps its property values,
and a listed item name the category does not hold is logged and skipped.

## Schema

[schema/ERHE_scene.schema.json](schema/ERHE_scene.schema.json)
(the `settings` object is validated by the erhe_codegen `Scene_settings`
schema, not duplicated here)
