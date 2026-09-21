# Brushes: outstanding work

Status: proposed

Extends `doc/editor/brushes.md`.

## A brush copy shares the original's geometry slot

`Brush::make_shared_payload_copy()` takes the geometry of the source brush
only when that brush is already `ready`, and hands the copy the generator
otherwise (`src/editor/brushes/brush.cpp`). A scene that copies the palette
into its own content library - Create Scene and the default scene, through
`copy_content_library()` - copies brushes that are still `unprepared`, so the
copy carries the recipe and prepares its own geometry, separately from the
palette original that a placement prepared. Two libraries then hold two meshes
of the same brush.

Make the copy share the source brush's geometry slot, so the recipe runs once
per brush however many libraries hold a copy of it and a preparation on either
side readies both.
