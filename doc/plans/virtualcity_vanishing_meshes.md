# VirtualCity.glb: overlapping meshes vanish on first viewport hover

Status: proposed

This plan extends `doc/editor/scene.md` (scene views, hover and raytrace
integration) with the diagnosis of one open defect.

Open, unexplained. It reproduces with `optimize_meshes=false`, so it is not a
meshoptimizer defect.

Symptom: after loading `VirtualCity.glb`, a few overlapping meshes sit in the
centre of the view and disappear the first time the mouse hovers the viewport.

The user suspects the cameras, and the evidence fits: the asset has 14 cameras
and exactly 14 animated nodes carrying meshes, the first named "cam01-box" -
camera helper boxes.

Read the asset's transforms with its unit scale in mind: the root node scales
by 0.0254 (inches to metres), so a node with local translation -753 and a
world AABB at -19 is consistent, not a stale transform.
