# graphics/

## Purpose

Editor-level graphics utilities: icon management, thumbnail generation, and gradient textures.

## Key Types

- **`Icon_set`** -- Manages icon atlases for the editor UI. Loads icon fonts and rasterizes icons at multiple sizes (small, large, hotbar). Provides `draw_icon()` and `add_icons()` for rendering type-specific icons in ImGui. Icons are used throughout the UI for items, tools, and content library entries.

- **`Thumbnails`** -- Generates small preview images for materials and brushes. Renders a sphere with each material into a small framebuffer. Updates incrementally (one thumbnail per frame). Used by content library UI and properties panel.

- **`Gradients`** (`gradients.hpp`) -- Gradient texture utilities.

## Public API / Integration Points

- `Icon_set::draw_icon()` -- draw an icon at current ImGui cursor position
- `Icon_set::add_icons()` -- add type-appropriate icons before an ImGui item
- `Thumbnails::update()` -- called once per frame to generate pending thumbnails

## Dependencies

- erhe::graphics, erhe::imgui
- editor: App_context, Programs, Mesh_memory
