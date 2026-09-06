# content_library/

## Purpose

Provides a hierarchical container for reusable editor assets: materials, brushes, animations, skins, and textures.

## Key Types

- **`Content_library`** -- Top-level container with a root `Content_library_node` and category folders (brushes, animations, skins, materials, textures, graph textures/meshes, physics items). Each `Scene_root` has its own `Content_library` and OWNS it: `Scene_root` calls `set_owner(this)`, and every owning entry's wrapped item reports that `Scene_root` from `erhe::Item_base::get_item_host()` (see `doc/content-library-ownership-plan.md`). An item is an owning member of exactly one library; `ERHE_VERIFY` enforces this on add. `Scene_builder`'s template library is never owned - scenes seed their own libraries with copies (`copy_content_library_folder`, `Brush::make_shared_payload_copy` shares the expensive payload). Prefab template textures/materials are the exception: they enter instancing scenes' libraries as REFERENCE entries (`Content_library_node::is_reference`) that never claim the item's host, because GPU textures cannot be duplicated per scene. `copy_library_item_to_library` copies a single item across libraries (also exposed as the `copy_library_item` MCP tool and the "Copy to Scene" context menu).

- **`Content_library_node`** -- Extends `erhe::Hierarchy`. Each node wraps an `erhe::Item_base` (e.g., a material or brush) or serves as a folder (no item, but has `type_code` and `type_name`). Features:
  - Typed `get_all<T>()` with internal caching; a cache covers the node's whole subtree, so an add or remove anywhere below a node clears the caches of that node and every ancestor
  - `combo<T>()` for ImGui combo boxes with drag-and-drop support
  - `add<T>()` / `remove<T>()` template methods; both find an existing entry anywhere in the subtree (`find_entry`), so an item is listed once per library no matter which folder holds it and a category folder removes an entry sitting in one of its folders
  - `make<T>()` to create and add a new item in one step
  - `make_folder()` to create sub-folders

- Every kind a library lists -- brush, style, material, texture, graph mesh, graph texture,
  animation, skin, physics material, collision filter, joint settings -- is a typed prim
  (`erhe::Typed`, `src/erhe/item/notes.md` "Prim classes"), so each carries a `typeName`
  token and can be parented in a prim tree. Nothing places one there yet: an item is still
  held by its `Content_library_node` entry, which is its inheritance container and its
  namespace.

- **`Material_library`** (`material_library.hpp`) -- Helper functions for populating default materials in a content library.

- **`Content_library_window`** (`content_library_window.hpp`) -- Owns an `Item_tree_window` displaying a `Content_library`. Wires up the "Create Material" context menu and cross-library material drag-drop. Constructed by callers (editor.cpp, asset_browser.cpp, operations_window.cpp) alongside a `Scene_root`; not owned by `Scene_root` itself.

## Folders

Folders below a category folder (`doc/content-library-folders.md`) are
`Content_library_node`s without an item that carry the category's
`type_code` / `type_name`. They are selectable, the Properties window shows
them as items (name row, class-chain properties, Add Property), and their
`inherits`-flagged property values reach the entries below them: the parent
folder's `handle_add_child` makes an owning entry node the inheritance
container of its item (`erhe::Item_base::set_inheritance_container`), the
node's destructor clears it, and `for_each_inheritance_child` visits the
item after the hierarchy children. A reference entry never becomes a
container - its item is owned by another scene. Entry names are
sibling-unique (`src/erhe/item/notes.md` "Sibling-unique names") and an
owning entry wins the name over a reference entry: `handle_add_child`
renames the colliding reference ENTRY NODE instead, so a scene's authored
names survive a reload whatever order the entries attach in. The editor
creates folders
("Create Folder", `create_library_folder`), moves entries between them
(drag onto a folder, `move_library_item`; `Content_library_move_operation`)
and persists them through `ERHE_scene` `library_folders`. A folder also
holds its category's item properties (`category_owner_type`, the folder's
secondary owner type, `doc/content-library-folders.md` D8): a Materials
folder offers `Material.base_color` and the other `Material` values in Add
Property, and a material below it without a local value reads them.

## Styles

The Styles category holds `Style` items (`style.{hpp,cpp}`,
`doc/style-library.md`): a style's secondary owner type is the root
owner type, so its own local values of any class are the style
(`Material.roughness`, `Light.color`) and the Properties window edits it
with the generic rows.
An item names its style through the `Item_base::style_property` row
(`Item_base::style_applies` decides which styles it can take); the
default metals share the "Brushed metal" style `add_default_materials`
creates here. "Create Style" on the Styles folder and the MCP
`create_style` make an empty style. `make_style_from_values`
(`operations/style_set_operation.hpp`) turns a bag of values into a style
item plus its assignment for "Paste Properties as Style" and the MCP
`set_item_style`; `copy_library_item_to_library` remaps or copies a
material's style into the target library. Styles save through
`ERHE_scene` `styles`; materials, nodes and folders name theirs.

## Importing texture files

Image files the editor can decode (`.png` / `.jpg` / `.jpeg` / `.ktx2` / `.dds`, see `is_texture_file_extension`) appear in the Asset Browser as `Asset_file_texture` items, and enter a scene's library through `import_texture_into_scene` (`assets/asset_workflow.hpp`) two ways:

- the browser's **"Import to content library texture"** context menu item - a plain item when one scene is open, a submenu of scene names when several are;
- **dropping** the file onto the target scene's Textures folder (or a texture in it) in the Scene Hierarchy window's nested Content Library.

Both queue an undoable `Content_library_attach_operation<erhe::graphics::Texture>` once the texture is resident. Every import creates a FRESH texture: an owning library entry claims its item's `Item_host`, so two libraries must never list the same object - importing the same file into two scenes gives each its own GPU texture.

Decoding and uploading go through `Texture_file_loader` (`graphics/texture_file_loader.hpp`): the decode runs on the executor, and `Editor::tick` creates the texture and records its upload against the frame's command buffer. The same loader keeps a bounded LRU cache of previews for the Asset Browser's file tooltip; the Content Library's texture tooltip needs no cache, its texture is already resident. Both draw through `draw_texture_preview`.

A standalone image file carries no usage information, so it is decoded as **sRGB**. A normal / ORM map imported this way is decoded as color data.

## Public API / Integration Points

- `Content_library_node::add<T>()` -- add an asset
- `Content_library_node::remove<T>()` -- remove an asset
- `Content_library_node::get_all<T>()` -- get all assets of a type (cached)
- `Content_library_node::combo<T>()` -- ImGui combo box for selecting an asset
- Used by `Scene_root`, `Scene_builder`, `Properties`, `Brush_tool`

## Dependencies

- erhe::item (Hierarchy, Item_base)
- erhe::scene (Animation, Camera, Light, Mesh, Skin)
- erhe::primitive (Material)
- editor: Icon_set (for combo box icons)
