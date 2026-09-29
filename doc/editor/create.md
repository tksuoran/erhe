# create/

Stability: stable

## Purpose

Provides the Create tool and shape generator classes for interactively creating new mesh primitives in the scene.

## Key Types

- **`Create`** -- A `Tool` that shows a panel with buttons for creating new shapes. Each button triggers a shape generator. Manages preview display, normal style, and density settings.

- **`Create_shape`** -- Abstract base for interactive shape creation. Subclasses provide ImGui controls for their parameters and generate a `Brush` when invoked.

- **`Create_uv_sphere`** -- UV sphere generator with configurable latitude/longitude subdivision.

- **`Create_cone`** -- Cone generator with configurable base/top radii, height, and subdivision.

- **`Create_capsule`** -- Capsule generator with configurable bottom/top radii (tapered capsules supported), mid-section length, and subdivision.

- **`Create_torus`** -- Torus generator with configurable major/minor radii and subdivision.

- **`Create_box`** -- Box generator with configurable dimensions and subdivision.

- **`Create_preview_settings`** -- Settings for preview rendering (ideal shape vs. subdivided shape; `draw_hidden`, the window's "Preview Hidden Lines" checkbox, draws the parts occluded by scene geometry as dimmed hidden lines, off draws only the unoccluded parts; `self_occluded_lines`, the "Preview Self-occluded Lines" checkbox, draws the parts behind the shape's own silhouette as thin minor lines, off draws only the self-visible parts).

## Public API / Integration Points

- `Create::tool_render()` -- renders shape preview in viewport
- Shape generators produce `Brush` objects that can be placed in the scene

## Dependencies

- erhe::geometry, erhe::primitive
- editor: Tool, App_context, Brush, Scene_root
