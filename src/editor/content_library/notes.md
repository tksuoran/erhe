# content_library/

## Purpose

Provides a hierarchical container for reusable editor assets: materials, brushes, animations, skins, and textures.

## Key Types

- **`Content_library`** -- Top-level container with a root `Content_library_node` and category folders (brushes, animations, skins, materials, textures, graph textures/meshes, physics items). Each `Scene_root` has its own `Content_library` and OWNS it: `Scene_root` calls `set_owner(this)`, and every owning entry's wrapped item reports that `Scene_root` from `erhe::Item_base::get_item_host()` (see `doc/content-library-ownership-plan.md`). An item is an owning member of exactly one library; `ERHE_VERIFY` enforces this on add. `Scene_builder`'s template library is never owned - scenes seed their own libraries with copies (`copy_content_library_folder`, `Brush::make_shared_payload_copy` shares the expensive payload). Prefab template textures/materials are the exception: they enter instancing scenes' libraries as REFERENCE entries (`Content_library_node::is_reference`) that never claim the item's host, because GPU textures cannot be duplicated per scene. `copy_library_item_to_library` copies a single item across libraries (also exposed as the `copy_library_item` MCP tool and the "Copy to Scene" context menu).

- **`Content_library_node`** -- Extends `erhe::Hierarchy`. Each node wraps an `erhe::Item_base` (e.g., a material or brush) or serves as a folder (no item, but has `type_code` and `type_name`). Features:
  - Typed `get_all<T>()` with internal caching (invalidated on add/remove)
  - `combo<T>()` for ImGui combo boxes with drag-and-drop support
  - `add<T>()` / `remove<T>()` template methods
  - `make<T>()` to create and add a new item in one step
  - `make_folder()` to create sub-folders

- **`Material_library`** (`material_library.hpp`) -- Helper functions for populating default materials in a content library.

- **`Content_library_window`** (`content_library_window.hpp`) -- Owns an `Item_tree_window` displaying a `Content_library`. Wires up the "Create Material" context menu and cross-library material drag-drop. Constructed by callers (editor.cpp, asset_browser.cpp, operations_window.cpp) alongside a `Scene_root`; not owned by `Scene_root` itself.

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
