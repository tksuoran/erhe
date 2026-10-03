# Mesh component selection: outstanding work

Status: proposed

This plan extends `doc/editor/mesh_component_selection.md` (face, edge and vertex
selection with its viewport overlay and gizmo transform) with the selection
scope it does not cover.

## Skinned meshes

Component selection on a skinned (deforming) mesh. CPU raytrace picking uses
bind-pose geometry, which does not match the deformed pose, so this needs
GPU-side picking (an ID render of components in the deformed pose) and
overlays rendered in the deformed pose (skinning the overlay positions).

## Compute selection over the vertex and index buffers

Compute-shader selection over the GPU vertex and index buffers themselves:
vertex and edge marking, and lasso selection. The region and brush FACE
selection already gathers on the GPU
(`doc/editor/mesh_component_selection.md` section 7); this extends the same idea to
the other component kinds.

## Multiview overlays

The triangle and point direct path of `Debug_renderer` is single-view only, so
the overlays render in the desktop viewport alone
(`doc/editor/mesh_component_selection.md` section 6). Lifting that needs a multiview
variant of the `line_simple` shader and per-eye view data on the direct path,
the way the wide-line compute path already has a multiview graphics stage.
