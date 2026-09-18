# VirtualCity.glb: overlapping meshes vanish on first viewport hover

Open, unexplained. Recorded 2026-08-27 during the meshoptimizer phase 7 sweep,
but NOT caused by it: it reproduces with `optimize_meshes=false`, so it
predates all of that work.

Symptom: after loading `VirtualCity.glb`, a few overlapping meshes sit in the
centre of the view and disappear the first time the mouse hovers the viewport.

The user suspects the cameras, and the evidence fits: the asset has 14 cameras
and exactly 14 animated nodes carrying meshes, the first named "cam01-box" -
camera helper boxes.

A wrong diagnosis was made and retracted: a node with local translation -753
and a world AABB at -19 is NOT a stale transform - the root node scales by
0.0254 (inches to metres).
