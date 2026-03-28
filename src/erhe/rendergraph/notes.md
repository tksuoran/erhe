# erhe_rendergraph

## Purpose

A directed acyclic graph (DAG) framework for organizing rendering operations.
Uses `erhe::graph` as the underlying graph infrastructure for topological sort
and pin/link connectivity. Nodes declare typed input and output pins identified
by integer keys. The graph is topologically sorted (with caching) and executed
each frame in dependency order, enabling modular composition of render passes
such as shadow maps, viewport rendering, post-processing, and GUI compositing.

## Key Types

- `Rendergraph` -- Owns the node list, performs topological sort via an
  internal `erhe::graph::Graph`, and executes the graph. Provides
  `connect(key, source, sink)` and `disconnect()` for wiring nodes.

- `Rendergraph_node` -- Abstract base class for graph nodes. Inherits from
  `erhe::graph::Node` (which provides pins, links, and Item identity) and
  `Texture_reference`. Has an enable flag and a pure virtual
  `execute_rendergraph_node()`.

- `Render_target` -- Composable helper that manages a color texture (possibly
  multisampled), depth/stencil texture, and `Render_pass` lifecycle. Nodes
  that need render targets own a `Render_target` member.

- `Texture_rendergraph_node` -- Node that owns a `Render_target`. Provides
  `get_producer_output_texture()` via the render target's color texture.
  Automatically registers an output pin for the configured key.

- `Rendergraph_node_key` -- Static constants defining well-known connection
  keys: `viewport_texture` (2), `shadow_maps` (3), `depth_visualization` (4),
  `texture_for_gui` (5), `rendertarget_texture` (6), `wildcard` (99).

## Architecture

### Inheritance

`Rendergraph_node` inherits directly from `erhe::graph::Node`, which provides
pin/link connectivity and participation in `erhe::graph::Graph` topological
sort. `Rendergraph_node` overrides the Item virtuals (`get_type()`,
`get_type_name()`, `clone()`) to provide its own type identity.

Pins are created via `register_input()` / `register_output()` which call
`base_make_input_pin()` / `base_make_output_pin()` on the node itself.
Links are managed by `Rendergraph::connect()` / `disconnect()`. Topological
sort is delegated to `erhe::graph::Graph::sort()` with caching
(`m_is_sorted` flag).

### Data Flow Model

Execution order is push-based: nodes are topologically sorted so producers
execute before consumers. Data access is pull-based: consumers call
`get_consumer_input_texture(key)` which traverses the pin/link graph to find
the upstream producer and calls `get_producer_output_texture(key)` on it.

Some data bypasses the graph entirely. Viewport dimensions flow from downstream
ImGui windows to upstream scene views via direct method calls
(`set_window_viewport()`). Light projections are accessed via `static_cast` to
`Shadow_render_node`. These side channels are practical and work correctly
because connection keys guarantee the source node type.

## Public API

### Creating and Wiring a Graph

```cpp
// Create the rendergraph
Rendergraph rendergraph{graphics_device};

// Nodes register themselves in their constructor
Shadow_render_node  shadow_node{rendergraph, ...};
Viewport_scene_view viewport{rendergraph, ...};
Post_processing_node post_processing{rendergraph, ...};

// Wire nodes together by key
rendergraph.connect(Rendergraph_node_key::shadow_maps,      &shadow_node, &viewport);
rendergraph.connect(Rendergraph_node_key::viewport_texture,  &viewport,   &post_processing);

// Each frame
rendergraph.execute(); // sorts if needed, then executes nodes in order
```

### Implementing a Node

Subclass `Rendergraph_node`, register pins in the constructor, and implement
`execute_rendergraph_node()`:

```cpp
class My_node : public Rendergraph_node
{
public:
    My_node(Rendergraph& rendergraph)
        : Rendergraph_node{rendergraph, erhe::utility::Debug_label{"My_node"}}
    {
        register_input ("input texture",  Rendergraph_node_key::viewport_texture);
        register_output("output texture", Rendergraph_node_key::viewport_texture);
    }

    void execute_rendergraph_node() override
    {
        auto input = get_consumer_input_texture(Rendergraph_node_key::viewport_texture);
        if (!input) return;
        // ... render using input texture, produce output ...
    }

    auto get_producer_output_texture(int key, int) const
        -> std::shared_ptr<erhe::graphics::Texture> override
    {
        if (key == Rendergraph_node_key::viewport_texture ||
            key == Rendergraph_node_key::wildcard) {
            return m_output_texture;
        }
        return {};
    }
};
```

### Using Texture_rendergraph_node

For nodes that render to a managed render target, inherit from
`Texture_rendergraph_node` instead. It automatically handles render target
creation and output pin registration:

```cpp
My_texture_node::My_texture_node(Rendergraph& rendergraph, ...)
    : Texture_rendergraph_node{Texture_rendergraph_node_create_info{
        .rendergraph          = rendergraph,
        .debug_label          = erhe::utility::Debug_label{"My_texture_node"},
        .output_key           = Rendergraph_node_key::viewport_texture,
        .color_format         = erhe::dataformat::Format::format_16_vec4_float,
        .depth_stencil_format = erhe::dataformat::Format::format_d32_sfloat_s8_uint,
        .sample_count         = 0
    }}
{
    register_input("shadow_maps", Rendergraph_node_key::shadow_maps);
}
```

### Accessing Connected Nodes

```cpp
// Get the upstream node for a given key
Rendergraph_node* producer = get_consumer_input_node(key);

// Get the upstream texture for a given key
auto texture = get_consumer_input_texture(key);

// Get the downstream node for a given key
Rendergraph_node* consumer = get_producer_output_node(key);
```

### Deferring Resource Destruction

When a node replaces resources during execution (e.g., resizing textures),
old resources may still be referenced by nodes that execute later in the
same frame. Use `defer_resource()` to keep them alive:

```cpp
void My_node::update_size()
{
    // Collect old resources before replacing them
    class Old_resources
    {
    public:
        std::shared_ptr<erhe::graphics::Texture>     old_texture;
        std::unique_ptr<erhe::graphics::Render_pass> old_render_pass;
    };
    auto old = std::make_shared<Old_resources>();
    old->old_texture     = std::move(m_texture);
    old->old_render_pass = std::move(m_render_pass);
    get_rendergraph().defer_resource(std::move(old));

    // Create new resources
    m_texture     = ...;
    m_render_pass = ...;
}
```

Deferred resources are cleared at the end of `Rendergraph::execute()`.

### Dynamic Connections

Connections can be changed at runtime. Both `connect()` and `disconnect()`
invalidate the sort cache:

```cpp
rendergraph.disconnect(Rendergraph_node_key::shadow_maps, old_shadow_node, viewport);
rendergraph.connect   (Rendergraph_node_key::shadow_maps, new_shadow_node, viewport);
```

## Pitfalls and Correctness Rules

### Resource Lifetime During Execution

**Problem:** A node that replaces its textures during `execute_rendergraph_node()`
can invalidate pointers held by nodes that execute later in the same frame.
ImGui draw data in particular stores raw `Texture_reference*` pointers that
are resolved lazily during `render_draw_data()`.

**Rule:** When replacing resources mid-execution, always defer destruction of
old resources via `get_rendergraph().defer_resource()`. Never destroy textures
or render passes that might still be referenced by downstream nodes in the
current frame.

### Destruction Order

**Problem:** `Rendergraph_node` stores a reference to its `Rendergraph`. If
the `Rendergraph` is destroyed first, the node's destructor will try to call
`unregister_node()` on a freed object.

**Rule:** `Rendergraph::~Rendergraph()` sets `m_is_registered = false` on
all nodes before destroying its members. `Rendergraph_node::~Rendergraph_node()`
checks `m_is_registered` before calling `unregister_node()`. This sentinel
check makes destruction order safe regardless of which object is destroyed
first.

### Pin Registration Timing

**Rule:** Register all input and output pins in the node constructor, after
the base class constructor has run (which calls `register_node()` and sets
up the proxy). Never register pins before the node is registered with the
rendergraph.

### Key Matching

**Rule:** `connect(key, source, sink)` requires that the source has an output
pin with the given key and the sink has an input pin with the given key. Both
pins must be registered before `connect()` is called.

### Wildcard Key

The `wildcard` key (99) is used by `Texture_reference::get_referenced_texture()`
to retrieve any output texture from a node without knowing the specific key.
Nodes that produce textures should handle the wildcard key in their
`get_producer_output_texture()` override:

```cpp
if (key == Rendergraph_node_key::viewport_texture ||
    key == Rendergraph_node_key::wildcard) {
    return m_texture;
}
```

### Multi-Host Rendering and Shared State

**Problem:** When multiple ImGui hosts (e.g., `Window_imgui_host` and
`Rendertarget_imgui_host`) share an `Imgui_renderer`, per-host operations
must not invalidate state needed by other hosts within the same frame.

**Rule:** Shared resources referenced across multiple rendergraph nodes in
the same frame must survive until all nodes have completed execution. The
`Imgui_renderer` handles this by retaining texture references across hosts
rather than clearing them per-host.

### Inputs and Outputs Allowed

Override `inputs_allowed()` or `outputs_allowed()` to return `false` for
nodes that should only produce or only consume. For example,
`Shadow_render_node` returns `false` from `inputs_allowed()` since it is
a pure source node. `register_input()` and `register_output()` check these
guards and log an error if violated.

### Sort Caching

The sort is cached via the `m_is_sorted` flag and only recalculated when the
topology changes. All of `register_node()`, `unregister_node()`, `connect()`,
and `disconnect()` invalidate the cache. Manual calls to `sort()` are
unnecessary -- `execute()` calls it internally.

### Traversal Depth Limit

Input/output node queries are capped at `rendergraph_max_depth` (10) to
prevent infinite recursion in case of unexpected cycles. If a node chain
exceeds this depth, the traversal returns nullptr.

## Editor Node Summary

| Node | Inputs | Outputs | Purpose |
|---|---|---|---|
| `Shadow_render_node` | none | `shadow_maps` | Renders shadow maps |
| `Viewport_scene_view` | `shadow_maps`, `rendertarget_texture` | `viewport_texture` | Renders 3D scene |
| `Post_processing_node` | `viewport_texture` | `viewport_texture` | Bloom + tonemapping |
| `Depth_to_color_rendergraph_node` | `shadow_maps` | `depth_visualization` | Debug shadow visualization |
| `Brdf_slice_rendergraph_node` | none | `texture_for_gui` | BRDF slice for UI |
| `Window_imgui_host` | `viewport_texture` | none | Renders ImGui to window |
| `Rendertarget_imgui_host` | none | `rendertarget_texture` | Renders ImGui to in-scene target |
| `Headset_view_node` | `shadow_maps`, `rendertarget_texture` | none | Renders scene to XR headset |

Typical connection chain:

```
Shadow_render_node --(shadow_maps)--> Viewport_scene_view
    --(viewport_texture)--> Post_processing_node
    --(viewport_texture)--> Window_imgui_host
```

## Dependencies

- erhe::graph (Graph, Node, Pin, Link for topology)
- erhe::graphics (Device, Texture, Render_pass, Renderbuffer, Swapchain)
- erhe::item (Item base class)
- erhe::math (Viewport)
- erhe::dataformat (Format for texture formats)
- erhe::utility (Debug_label)
- erhe::profile (profiling mutex)
- glm

## Implementation Notes

- `Render_target` handles MSAA resolve internally when `sample_count > 0`.
- The `none` key (0) in `Texture_rendergraph_node_create_info` means "do not
  register an output pin". Used for nodes that conditionally produce output.
- `automatic_layout()` positions nodes for the rendergraph visualization
  window based on depth and column assignment.
