# erhe_rendergraph

## Purpose
A directed acyclic graph (DAG) framework for organizing rendering operations. Nodes declare typed input and output connectors identified by integer keys. The graph is topologically sorted and executed each frame in dependency order, enabling modular composition of render passes such as shadow maps, viewport rendering, post-processing, and GUI compositing.

## Key Types
- `Rendergraph` -- Owns all nodes, performs topological sort, and executes the graph. Provides `connect(key, source, sink)` and `disconnect()` for wiring nodes.
- `Rendergraph_node` -- Abstract base class for graph nodes. Has named input/output connectors, an enable flag, and a pure virtual `execute_rendergraph_node()`. Extends `erhe::Item` and `Texture_reference`.
- `Rendergraph_producer_connector` / `Rendergraph_consumer_connector` -- Connector descriptors storing a key, label, and list of connected peer nodes.
- `Texture_rendergraph_node` -- Concrete node that owns a color texture, optional depth/stencil texture, and a `Render_pass`. Manages multisampled textures and resizing.
- `Sink_rendergraph_node` -- Terminal node that only consumes input (no outputs). Used for final presentation.
- `Rendergraph_node_key` -- Static constants defining well-known connection keys: `viewport_texture`, `shadow_maps`, `depth_visualization`, `texture_for_gui`, `rendertarget_texture`.

## Public API
- Create `Rendergraph`, register nodes, connect them with `connect(key, source, sink)`.
- Call `rendergraph.sort()` then `rendergraph.execute()` each frame.
- Subclass `Rendergraph_node` and implement `execute_rendergraph_node()`.
- Use `get_consumer_input_texture(key)` / `get_producer_output_texture(key)` to pass textures between nodes.

## Dependencies
- erhe::graphics (Device, Texture, Render_pass, Renderbuffer, Swapchain)
- erhe::item (Item base class)
- erhe::math (Viewport)
- erhe::dataformat (Format for texture formats)
- erhe::utility (Debug_label)
- erhe::profile (profiling mutex)
- glm

## Notes
- Max traversal depth for input/output queries is capped at `rendergraph_max_depth` (10).
- `Texture_rendergraph_node` handles MSAA resolve internally when `sample_count > 0`.
- The editor builds its render pipeline by connecting shadow, viewport, post-process, and ImGui nodes.
