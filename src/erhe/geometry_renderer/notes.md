# erhe_geometry_renderer

## Purpose
Debug visualization of `erhe::geometry::Geometry` meshes. Renders wireframe edges,
vertex labels, and facet information using line and text renderers for debugging
geometry operations in 3D viewports.

## Key Types
No classes; exposes a single free function.

## Public API
- `debug_draw(viewport, clip_from_world, line_renderer, text_renderer, world_from_local, geometry, facet_filter)` -- Draw debug visualization of a geometry mesh.

## Dependencies
- **erhe libraries:** `erhe::geometry` (public), `erhe::math` (public), `erhe::renderer` (public), `erhe::profile`, `erhe::verify`
- **External:** glm

## Notes
- The `facet_filter` parameter allows rendering only specific facets (by Geogram index).
- Uses `Primitive_renderer` for line drawing and `Text_renderer` for labels.
- This is a small single-function library primarily used for editor debugging.
