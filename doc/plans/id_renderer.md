# ID renderer coverage

Status: proposed

This plan extends `doc/editor/rendering.md` ("ID renderer") with the coverage
its picking path still lacks.

`src/editor/renderers/id_renderer.{hpp,cpp}`, its pipeline setup in
`App_rendering` and its per-viewport call in
`Viewport_scene_view::execute_rendergraph_node` have no automated coverage and
have not been exercised since the shader-variant / multi-vertex-format work.
The failure modes to look for are mouse picking that returns no hit, a wrong
hit or random IDs, and an abort on the first picking attempt.

Acceptance: a left click on a mesh in the 3D viewport selects it; the readback
ring (4 entries) cycles without stalling; the primitive index and triangle ID
match the picked surface.
