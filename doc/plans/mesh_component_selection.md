# Mesh component selection: outstanding work

Status: proposed

This plan extends `doc/editor/mesh_component_selection.md` (face, edge and vertex
selection with its viewport overlay) with the editing and the wider selection
scope it does not cover.

## Transform selected vertices

Move, rotate and scale the selected vertices, and the implied vertices of
selected edges and faces. This needs a transform pivot derived from the
selection, integration with the existing transform tools and `Operation_stack`
for undo, writing the transformed positions back into the `Geometry`, and
re-uploading or rebuilding the affected `Primitive` GPU buffers.

The component selection has to survive the edit, so the geometry-identity
invalidation of `doc/editor/mesh_component_selection.md` section 3 has to relax to an
index remap for an in-place edit.

## Set vertex attribute values

Edit per-vertex and per-corner attributes (color, UV, custom) on the
selection, extending the per-corner editing `Paint_tool` already performs for
vertex colors. Needs a small attribute-editing UI and the same write-back and
re-upload path as the transform above.

## Multiple meshes

Let a component selection span several meshes at once. The data model becomes
a map keyed by (mesh, primitive index) instead of a single active mesh, and
the rendering iterates the map.

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
