# ERHE_scene

Stability: mostly stable

## Scope

**Scene** extension. Optional (`extensionsUsed` only).

## Overview

Marks a file as an erhe-authored scene and carries per-scene editor state.
Its presence in `extensionsUsed` is the discriminator between "open as a
full erhe scene" and "import as an asset": scene saves always write it,
plain interchange exports never do.

- `ambient_light`: the effective scene ambient light color as four numbers,
  the fourth `0` (erhe #237). The load reads the first three and makes them
  the scene item's local value.
- `properties`: the scene item's local property values as a name to text
  map, the form of `ERHE_node` `properties` (the registered properties of
  `erhe::scene::Scene` by name, `doc/erhe/property_system.md` section 4.20).
  The map is the item's COMPLETE local set: on load a value the
  `ambient_light` field above made local that the map does not name is
  cleared again, so an ambient color held by a style is still style-held
  after a reload. Written even when empty, which is what tells a reader
  that the file carries the map; a file without it (written before the map
  existed) leaves `ambient_light` local.
- `style` (optional): the name of the style item the scene uses
  (`doc/editor/style_library.md` D3), assigned after `styles` below has
  created every style of the file. Omitted when the scene uses none.
- `enable_physics`: whether the scene owns a physics world.
- `settings` (optional): per-scene overrides of editor-global settings
  (erhe #239) as a `Scene_settings` JSON object (the erhe_codegen schema in
  `src/editor/scene/definitions/scene_settings.py`; each absent / null
  field means "use the editor-global default"). Omitted entirely when no
  override is engaged.
- `styles` (optional): the content library's style items
  (`doc/editor/style_library.md` D4): `name` and `properties` (the style's local
  values as a name to text map, the form of `ERHE_node` `properties`,
  keyed by qualified name such as `Material.roughness` or `Light.color`;
  omitted when empty), and `style` (optional), the name of the style that
  style uses itself (`doc/erhe/property_system.md` D25 style chain; omitted
  when it uses none). A `target` member of older files is ignored. The
  styles are loaded before anything that names a style, and a `style`
  member is assigned after every entry of the array exists, so the order
  inside the array does not matter. Omitted when the library has no
  styles.
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
- `physics_joints` (optional): one entry per `KHR_physics_rigid_bodies`
  `physicsJoints` entry, by index (a `PhysicsJoint` carries no name):
  `name` and `properties` (the joint-settings item's local property values
  as a name to text map, the form of `ERHE_node` `properties`; the
  registered properties of `Physics_joint_settings` by name, the eleven
  per-axis limit and drive values of each of the six degrees of freedom).
  The map is the item's complete local set: on load, a value the KHR entry
  carried that the map does not name is cleared again, so an axis value the
  item inherits from its folder or takes from a style still does after a
  reload. This is also where a joint-settings item's name lives, since the
  KHR entry has no name field. Omitted when the file has no joints.
- `collision_filter_names` (optional): the names of the
  `KHR_physics_rigid_bodies` `collisionFilters` entries, by index, so the
  content library's collision filters keep their names across a save and
  reload. Omitted when the file has none.
- `library_folders` (optional): where the scene's content-library resources
  sit in the scene tree (`doc/editor/content_library_folders.md` D5), parents before
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
    "ambient_light": [0.1, 0.1, 0.12, 0],
    "properties": {"ambient_light": "0.1 0.1 0.12"},
    "style": "Ambience",
    "enable_physics": true,
    "settings": {
        "post_processing": false,
        "clear_color": [0, 0, 0, 1]
    },
    "styles": [
        {"name": "Metal", "properties": {"Material.metallic": "1"}},
        {"name": "Brushed metal", "properties": {"Material.roughness": "0.34 0.2"}, "style": "Metal"}
    ],
    "physics_materials": [
        {"name": "Rubber", "properties": {"restitution": "0.8", "linear_damping": "0.1"}}
    ],
    "physics_joints": [
        {"name": "Hinge", "properties": {"rot_z_limit": "limited", "rot_z_limit_min": "-0.785398"}}
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
