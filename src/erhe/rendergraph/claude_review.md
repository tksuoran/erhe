# erhe_rendergraph -- Code Review

## Summary
A well-designed render graph system implementing a DAG of nodes with typed input/output connectors, topological sorting, and automatic execution. The architecture is clean and the separation between the graph management (`Rendergraph`), node base class (`Rendergraph_node`), and specialized nodes is solid. The main concerns are a quadratic topological sort algorithm and a syntax error in disabled code.

## Strengths
- Clean DAG abstraction with producer/consumer connectors and key-based routing
- Topological sort with good cycle detection and diagnostic logging
- Proper mutex usage for concurrent node registration/unregistration
- `Texture_rendergraph_node` cleanly manages framebuffer lifecycle with resize detection
- `Sink_rendergraph_node` properly enforces no-output constraint via `outputs_allowed()` override
- Depth propagation through `set_depth()` automatically maintains ordering invariants
- Automatic layout algorithm for GUI visualization of the render graph

## Issues
- **[moderate]** Topological sort in `rendergraph.cpp` is O(n^2) -- it restarts the outer loop on every successful node selection. For a large graph this could be slow. A Kahn's algorithm with an in-degree counter would be O(V+E).
- **[moderate]** `rendergraph.cpp:306` in the `#if 0` disabled code block has a syntax error: `average_source_position(lhs)` uses a `:` instead of `;` at the end of the function. While harmless since the code is disabled, it would prevent re-enabling.
- **[moderate]** `resource_routing.cpp` and `resource_routing.hpp` define `Rendergraph_node_key` constants but `resource_routing.cpp` is completely empty -- the header is effectively the entire "module". The key constants have a misleading comment on `wildcard` (says "imgui host texture rendered in 3d viewport" which is a copy-paste from `rendertarget_texture`).
- **[minor]** `rendergraph_node.cpp:59` in `get_input()` references `c_str(resource_routing)` in a SPDLOG_LOGGER_TRACE call, but `resource_routing` is not defined in scope. This only works because SPDLOG_LOGGER_TRACE is likely compiled out.
- **[minor]** `Rendergraph_node` inherits from both `erhe::Item` and `erhe::graphics::Texture_reference` -- the `Texture_reference` inheritance feels overly coupled; not all rendergraph nodes produce textures.
- **[minor]** `connect_input()` allows at most one producer per input (returns false if already connected) but the check after the empty-check is unreachable since it was already established the list is non-empty.

## Suggestions
- Replace the topological sort with Kahn's algorithm for better asymptotic performance
- Fix the copy-paste error in the `wildcard` comment in `resource_routing.hpp`
- Consider separating `Texture_reference` from the base `Rendergraph_node` class -- only nodes that produce textures should implement it
- Clean up the stale `resource_routing` reference in the trace log at `rendergraph_node.cpp:59`
- Remove or fix the `#if 0` code block in `rendergraph.cpp` -- either complete it or delete it
