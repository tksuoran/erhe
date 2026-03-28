# erhe_rendergraph -- Code Review

## Summary
A render graph system implementing a DAG of nodes with typed input/output pins, topological sorting (delegated to `erhe::graph`), and automatic execution. The architecture uses a proxy pattern where each `Rendergraph_node` has a corresponding `erhe::graph::Node` for topology management.

## Strengths
- Clean DAG abstraction using `erhe::graph` for pin/link connectivity and topological sort
- Sort caching (`m_is_sorted` flag) avoids redundant sorts each frame
- Proper mutex usage for concurrent node registration/unregistration
- `Render_target` cleanly manages framebuffer lifecycle with resize detection as a composable helper
- Depth propagation through `set_depth()` automatically maintains ordering invariants
- Automatic layout algorithm for GUI visualization of the render graph

## Remaining Considerations
- **[minor]** `Rendergraph_node` inherits from both `erhe::Item` and `erhe::graphics::Texture_reference` -- the `Texture_reference` inheritance means all rendergraph nodes are texture references even if they don't produce textures. This is used by the `get_referenced_texture()` override which returns the wildcard output texture.
- **[minor]** `resource_routing.cpp` is empty -- `Rendergraph_node_key` is header-only. The .cpp could be removed but is harmless.
