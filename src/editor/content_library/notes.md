# content_library/

## Purpose

Provides a hierarchical container for reusable editor assets: materials, brushes, animations, skins, and textures.

## Key Types

- **`Content_library`** -- Top-level container with a root `Content_library_node` and category folders (brushes, animations, skins, materials, textures). Each `Scene_root` has its own `Content_library`.

- **`Content_library_node`** -- Extends `erhe::Hierarchy`. Each node wraps an `erhe::Item_base` (e.g., a material or brush) or serves as a folder (no item, but has `type_code` and `type_name`). Features:
  - Typed `get_all<T>()` with internal caching (invalidated on add/remove)
  - `combo<T>()` for ImGui combo boxes with drag-and-drop support
  - `add<T>()` / `remove<T>()` template methods
  - `make<T>()` to create and add a new item in one step
  - `make_folder()` to create sub-folders

- **`Material_library`** (`material_library.hpp`) -- Helper functions for populating default materials in a content library.

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
