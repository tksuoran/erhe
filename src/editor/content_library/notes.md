# content_library/

## Purpose

Indexes a scene's reusable resources - materials, brushes, styles, textures, physics items, animations, skins and node graphs - which live as prims in the scene's own tree.

## Key Types

- **`Content_library`** -- The per-scene index of a scene's resources
  (materials, textures, brushes, styles, physics materials, collision
  filters, joint settings, animations, skins, geometry and texture graphs).
  It owns no resource: a resource is an `erhe::Typed` prim of the scene's
  prim tree (`doc/usd-compatibility-plan.md` C5, U4), under the `Scope`
  named for its kind or under any other prim the user puts it under, and
  reports the owning `Scene_root` from `erhe::Item_base::get_item_host()`.
  Each `Scene_root` has its own `Content_library` and gives it the scene root
  node (`set_owner(this, root_node)`); a library with no scene (the
  `Scene_builder` template palette) keeps its own detached root scope and
  hosts its prims itself, so one hook maintains the index in both cases.
  A resource is a member of exactly one library - scenes seed their own
  libraries with copies (`copy_content_library`,
  `Brush::make_shared_payload_copy` shares the expensive payload), and
  `copy_library_item_to_library` copies one resource across libraries (also
  exposed as the `copy_library_item` MCP tool and the "Copy to Scene" context
  menu). Prefab template textures / materials are the exception: they belong
  to the template's own tree, so the instancing scene lists them with
  `add_referenced()` - an index entry with no prim placement - so its
  material set gives them slots.

- **Kind scopes** -- `get_scope(kind_type_bit)` answers the `erhe::Scope` a
  resource of that kind is placed under (`Materials`, `Textures`, `Brushes`,
  `Styles`, `Physics Materials`, `Collision Filters`, `Physics Joints`,
  `Animations`, `Skins`, `Graph Meshes`, `Graph Textures`), creating it on
  the first resource of that kind and keeping it afterwards - a kind no
  resource ever reached adds no prim, and a folder the user made under a
  scope survives emptying it. A kind scope and the resource prims below it
  carry `Item_flags::show_in_ui` but not `Item_flags::content`, which is what
  keeps the glTF node writer from writing them as nodes
  (`doc/scene_serialization.md`).

## The index

The index answers the queries consumers ask without a tree walk. One list per
resource kind is filled and emptied by `register_prim()` / `unregister_prim()`
- `erhe::Item_host`'s prim hook (`src/erhe/item/notes.md` "Prim classes"),
which the owning `Scene_root` forwards here. Each kind's list carries a serial
that moves whenever the list does, and `get_all<T>()` returns a reference to a
typed vector it rebuilds only when that serial moved, so a per-frame consumer
(`App_scenes::update_material_sets` reconciling the scene's `Material_set`)
walks nothing while the library is unchanged. `has_item()` answers from a
by-item set and `get_all_of_kind()` gives one kind's resources without naming
their class (the asset manager's `scene_local` resolution asks by kind bit,
`Content_library::get_kind_type_bit_of_type`). A move between scopes of one
scene keeps the prim's item host, so the hook does not fire and the index is
untouched - a move is not a removal.

`Resource_metadata` is what the library keeps beside a resource: where it came
from (`gltf_source`, a texture's retained `image_source`), how its defining
container addresses it (`asset_key`), its declared usership with the asset
manager (`asset_usership`, R5.6), and whether it is a referenced listing
rather than an owned prim (`is_reference`). It is bookkeeping of the LIBRARY,
keyed by the resource and guarded by a `weak_ptr`, and it outlives the index
entry: an undo takes a resource prim out of the tree and a redo puts it back,
and the bookkeeping must survive that.

- **`Material_library`** (`material_library.hpp`) -- Helper functions for populating default materials in a content library.

## Folders

A folder is an `erhe::Scope` below a kind scope
(`doc/content-library-folders.md`). Folders are selectable, the Properties
window shows them as items (name row, class-chain properties, Add Property),
and their `inherits`-flagged property values reach the resources below them by
the ordinary tree parent - a resource prim's inheritance parent is its scope,
so no library-specific inheritance link exists. A `Scope`'s secondary owner
type is the root owner type, so it holds any class's values by qualified name
and its Add Property list offers the classes of the prims below it first
(`doc/content-library-folders.md` D8): a Materials folder offers
`Material.base_color` and the other `Material` values, and a material below it
without a local value reads them. Resource and folder names are
sibling-unique (`src/erhe/item/notes.md` "Sibling-unique names") by the tree.
The editor creates folders ("Create Scope", `create_library_folder`), moves
resources between them (drag onto a scope, `move_library_item`;
`Content_library_move_operation`) and persists them through `ERHE_scene`
`library_folders`.

## Styles

The Styles scope holds `Style` items (`style.{hpp,cpp}`,
`doc/style-library.md`): a style's secondary owner type is the root
owner type, so its own local values of any class are the style
(`Material.roughness`, `Light.color`) and the Properties window edits it
with the generic rows.
An item names its style through the `Item_base::style_property` row
(`Item_base::style_applies` decides which styles it can take); the
default metals share the "Brushed metal" style `add_default_materials`
creates here. "Create Style" on the Styles scope and the MCP
`create_style` make an empty style. `make_style_from_values`
(`operations/style_set_operation.hpp`) turns a bag of values into a style
item plus its assignment for "Paste Properties as Style" and the MCP
`set_item_style`; `copy_library_item_to_library` remaps or copies a
material's style into the target library. Styles save through
`ERHE_scene` `styles`; materials, nodes and folders name theirs.

## Importing texture files

Image files the editor can decode (`.png` / `.jpg` / `.jpeg` / `.ktx2` / `.dds`, see `is_texture_file_extension`) appear in the Asset Browser as `Asset_file_texture` items, and enter a scene's library through `import_texture_into_scene` (`assets/asset_workflow.hpp`) two ways:

- the browser's **"Import to content library texture"** context menu item - a plain item when one scene is open, a submenu of scene names when several are;
- **dropping** the file onto the target scene's `Textures` scope (or a texture in it) in the Scene Hierarchy window.

Both queue an undoable `Content_library_attach_operation<erhe::graphics::Texture>` once the texture is resident. Every import creates a FRESH texture: an owned resource is a prim of its scene's tree and reports that scene as its `Item_host`, so two libraries must never own the same object - importing the same file into two scenes gives each its own GPU texture.

Decoding and uploading go through `Texture_file_loader` (`graphics/texture_file_loader.hpp`): the decode runs on the executor, and `Editor::tick` creates the texture and records its upload against the frame's command buffer. The same loader keeps a bounded LRU cache of previews for the Asset Browser's file tooltip; the Hierarchy window's texture tooltip needs no cache, its texture is already resident. Both draw through `draw_texture_preview`.

A standalone image file carries no usage information, so it is decoded as **sRGB**. A normal / ORM map imported this way is decoded as color data.

## Public API / Integration Points

- `Content_library::add<T>()` / `make<T>()` -- place a resource prim under its kind scope
- `Content_library::remove<T>()` -- take a resource out of the tree (or out of the referenced listings)
- `Content_library::get_all<T>()` / `get_all_of_kind()` / `has_item()` -- the index
- `Content_library::get_scope()` / `find_scope()` / `find_scope_kind()` -- the kind scopes
- `Content_library::add_referenced()` / `remove_referenced()` / `is_referenced()` -- resources another container owns
- `Content_library::combo<T>()` -- ImGui combo box for selecting a resource
- Used by `Scene_root`, `Scene_builder`, `Properties`, `Brush_tool`

## Dependencies

- erhe::item (Hierarchy, Item_base)
- erhe::scene (Animation, Camera, Light, Mesh, Skin)
- erhe::primitive (Material)
- editor: Icon_set (for combo box icons)
