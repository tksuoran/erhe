# Raster occlusion culling

Status: proposed

This plan extends `doc/editor/rendering.md` (the composer passes and the
forward renderer) with depth-buffer-based occlusion culling.

Sketch:

1. Render the list of visible objects.
2. Rasterize the bounding volumes of all objects against the depth buffer from
   step 1.
3. Declare `layout(early_fragment_tests) in;` in the fragment shader so early-z
   happens; the scheme depends on it.
4. In the fragment shader, write a bit to an SSBO to mark the current object
   visible, so one visible fragment of a bounding volume marks the whole
   object. Objects that were not visible and just turned visible get a second
   bit saying they should be rendered this frame.
5. Render the objects that just became visible, to avoid one frame of occlusion
   lag. Optional where minor pop-in is acceptable.
6. Repeat for frame N+1, which consumes the visibility this frame generated.
7. Read the list of visible objects with MDI and generate the draws from it.
   This step is what makes the scheme worthwhile; without it the result is a
   worse form of hardware occlusion queries.
