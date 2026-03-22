# brushes/

## Purpose

Implements the brush system for placing parametric mesh shapes onto surfaces.

## Key Types

- **`Brush`** -- A reusable mesh template (Item_base subtype). Holds a `Brush_data` with geometry (or a geometry generator), build info, normal style, collision shape, density, and volume. Supports scale-dependent caching (`Scaled` entries) for GPU primitives and collision shapes at different scales. Provides `make_instance()` to create a scene node from the brush. Computes reference frames for face-aligned placement.

- **`Brush_data`** -- Configuration for creating a `Brush`: name, geometry, build info, collision shape generator, density.

- **`Brush_tool`** -- A `Tool` for placing brushes on hovered surfaces. Handles:
  - Preview mesh display (translucent ghost of the brush at the placement position)
  - Snapping to hovered polygon face or grid
  - Scale-to-match (match face size of target)
  - Insert operation (creates an `Item_insert_remove_operation` via `Operation_stack`)
  - Brush rotation (cycle through face offsets)
  - Brush picking (select brush from hovered mesh)
  - Drag-and-drop brush placement from content library

- **`Reference_frame`** -- Computes a coordinate frame for a specific polygon face, used to align a brush to a surface. Defined by a facet index and corner offset.

- **`Brush_placement`** -- A `Node_attachment` that records how a brush was placed (which brush, which face, which corner offset).

## Public API / Integration Points

- `Brush::make_instance()` -- create a scene node from this brush
- `Brush::get_reference_frame()` -- get placement coordinate frame for a face
- `Brush_tool::try_insert()` -- place the current brush
- `Brush_tool::preview_drag_and_drop()` -- handle drag-and-drop from content library

## Dependencies

- erhe::geometry, erhe::primitive, erhe::physics, erhe::scene
- editor: App_context, Operation_stack, Tools, Scene_root, Icon_set
