# erhe_rendergraph

Stability: stable

## Purpose

A directed acyclic graph (DAG) framework for organizing rendering operations.
Uses `erhe::graph` as the underlying graph infrastructure for topological sort
and pin/link connectivity. Nodes declare typed input and output pins identified
by integer keys. The graph is topologically sorted (with caching) and executed
each frame in dependency order, enabling modular composition of render passes
such as shadow maps, viewport rendering, post-processing, and GUI compositing.

## Key Types

- `Rendergraph` -- Owns the live node list, performs topological sort via
  an internal `erhe::graph::Graph`, prepares each frame (`prepare_frame()`:
  one node list slot per frame in flight) and executes the frame's slot.
  Provides `connect(key, source, sink)` and `disconnect()` for wiring
  nodes, and decides when a released node is destroyed ("Node lifetime").

- `Rendergraph_node` -- Abstract base class for graph nodes. Inherits from
  `erhe::graph::Node` (which provides pins, links, and Item identity) and
  `Texture_reference`. Has an enable flag and connections (live, plus one
  record per frame in flight; "Per-frame copies" below), a pure virtual
  `execute_rendergraph_node()`, a virtual `prepare_rendergraph_node()`
  hook, a virtual `pre_execute_rendergraph_node()` hook ("Pre-execute
  hook" below) and a virtual `on_released()` hook. Created only through
  `make_rendergraph_node<T>(...)`.

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

// Nodes register themselves in their constructor; make_rendergraph_node()
// gives the shared_ptr the deleter that lets the rendergraph decide when the
// node is destroyed ("Node lifetime")
std::shared_ptr<Shadow_render_node>   shadow_node     = make_rendergraph_node<Shadow_render_node>  (rendergraph, ...);
std::shared_ptr<Viewport_scene_view>  viewport        = make_rendergraph_node<Viewport_scene_view> (rendergraph, ...);
std::shared_ptr<Post_processing_node> post_processing = make_rendergraph_node<Post_processing_node>(rendergraph, ...);

// Wire nodes together by key
rendergraph.connect(Rendergraph_node_key::shadow_maps,      shadow_node.get(), viewport.get());
rendergraph.connect(Rendergraph_node_key::viewport_texture, viewport.get(),    post_processing.get());

// Each frame
rendergraph.prepare_frame(); // sorts if needed, copies the node list, enabled flags, connections (and node parameters) into the frame
rendergraph.execute(command_buffer); // the frame's enabled nodes: every pre-execute hook, the callback, then every execute, in order
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
`defer_resource()` is render phase only (verified with
`erhe::utility::Thread_roles`): a resource deferred from another thread
would race with that release.

### Per-frame copies

The node list, the enable flags, the connections and anything a node reads
from state other threads edit reach the render phase as copies made for
its frame, so the prepare phase of a later frame, or the editor's ImGui
phase, never writes what an earlier frame's render phase reads:

- `Rendergraph_node::set_enabled()` / `is_enabled()` are the live flag
  (main thread); `register_node()` (from the node constructor),
  `connect()` and `disconnect()` edit the live list and graph (main
  thread, under the graph mutex). `Rendergraph::prepare_frame()` (main
  thread, prepare phase, after every edit of the frame) sorts the live
  list, calls `prepare_rendergraph_node(frame_index)` on every enabled
  node, then copies the live list, in execution order, into the
  rendergraph's slot of the frame and every node's live flag and the
  first link of each of its pins into the node's record of the frame
  (one slot and one record per device frame in flight, keyed by the
  role-keyed `Device::get_frame_index()`; sized once, refilled in place).
  A hook may change live flags, its own or another node's (the editor's
  scene image capture disables its chain once the readback was
  recorded): the copy follows every hook.
- `Rendergraph::execute()` runs exactly the nodes of its frame's slot
  that are enabled in their record (`is_enabled_in_frame(frame_index)`),
  and verifies that the slot was filled for the frame it renders. A node
  registered, or a connection made, after the frame's copy takes part from
  the next frame. A program without a separate prepare phase calls
  `prepare_frame()` right before `execute()`.
- In the render phase (`Frame_phase::r_render`),
  `get_consumer_input_node()` / `get_producer_output_node()` (and the
  texture lookups built on them) answer from the node's record of the
  frame being rendered: a node that frame does not name has no
  connections in it (nullptr). In every other phase they read the live
  graph.
- The graph mutex guards the live list, the graph, the slots while
  `prepare_frame()` fills them and the retired list. `execute()` never
  takes it, so the prepare phase of the next frame (and any register,
  connect or release) runs while a frame renders.
- A node's `prepare_rendergraph_node()` copies the live parameters its
  execute reads into its own slot of the frame (the editor's
  `Post_processing_node` parameters and BRDF slice angles), and its
  execute reads that slot.

### Pre-execute hook

`Rendergraph::execute()` runs in three steps on the render thread, with
the frame command buffer and outside any render pass: first
`pre_execute_rendergraph_node(command_buffer)` of every node of the frame's
slot that is enabled in it, in execution order; then the pre-execute
callback (`Rendergraph::set_pre_execute_callback()`, set once on the main
thread before the first `prepare_frame()`, verified; none by default); then
`execute_rendergraph_node()` of the same nodes in the same order. So every
hook of the frame has run before the first node executes. The hook is for
work every node's execute depends on that a node's own execute would be too
late for: the editor's draw-list nodes add their passes' culling views in
it (the shadow nodes fit their light projections there too), and the
callback classifies every view of the frame at once, the GPU path recording
its compute dispatch there (doc/erhe/draw_list_renderer.md Q6,
doc/plans/culling.md phase 6a). The default hook does nothing.
`erhe_rendergraph_gpu_tests` checks the order.

### Resizing from the Prepare Phase

A `Texture_rendergraph_node` whose size the main thread decides takes the
size as a request: `request_size(width, height)` (main thread, prepare
phase) stores it in the slot of the frame being prepared, one slot per
frame in flight, and `apply_requested_size()` at the start of the node's
`execute_rendergraph_node()` sizes the render target to the request of the
frame it renders (a frame without a request keeps the current size). The
render target's textures and render pass are then created, replaced and
dropped by the render phase only, so the prepare phase of a later frame
never replaces an object an earlier frame's render phase is reading.
`Render_target::get_sample_count()` reports the sample count the render
target builds for (`reconfigure()` changes it).

### Node lifetime

A frame the render phase is still rendering may name a node whose owner
has already released it. Every node is therefore created by
`make_rendergraph_node<T>(args...)`, a `std::shared_ptr<T>` whose deleter
(`Rendergraph_node_deleter`) hands the node back to the rendergraph
(`prepare_frame()` verifies every registered node was made this way):

- When the last owner releases the node, the rendergraph unregisters it at
  once (it leaves the live list, its links are disconnected, later frames
  do not name it). If no slot of a frame not yet rendered names it, it is
  destroyed right there; otherwise it is retired with the latest frame
  that names it.
- `execute()` records the frame it finished (under a mutex of its own).
  The next `prepare_frame()` destroys the retired nodes whose frame has
  been rendered, outside the graph mutex (a destroyed node may release the
  nodes it owns). Its execute never runs after the release: the frames
  prepared later do not name it.
- `end_rendering()` (shutdown, after the last render phase) destroys every
  retired node and makes later releases destroy at once.
  `~Rendergraph()` clears the nodes' registered flag, so a node released
  after the rendergraph is gone is destroyed directly.
- Retirement keeps the node object alive, not what its owner held beside
  it: the owner's other members are destroyed with the owner, while the
  retired node's execute of the frame in flight still runs. So an execute
  reaches such resources only through the node's own per-frame copy, made
  by `prepare_rendergraph_node()` in the prepare phase of the frame
  (`Prepared_frame_values`, doc/editor/render_thread.md). Precedent: the
  editor's `Rendertarget_imgui_host` copies the shared pointer to the
  scene rendertarget mesh it draws into (render pass and texture) into the
  frame's slot; a scene close destroys the mesh's owner (`Quad_view`) on
  the main thread while the frame renders, and the slot keeps the mesh
  alive until the node is destroyed.
- The deleter first calls the node's release hook,
  `Rendergraph_node::on_released()` (virtual, default nothing), on the
  releasing thread, before the rendergraph unregisters it. A node that
  registered itself with a registry outside the rendergraph that a prepare
  or ImGui phase iterates deregisters there (an `Imgui_host` from the
  `Imgui_renderer`, the editor's `Scene_image_view` from `App_rendering`),
  so no later phase reaches a retired node; its destructor runs when the
  node is destroyed.

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
the `Rendergraph` is destroyed first, the node's release must not call into
a freed object.

**Rule:** `Rendergraph::~Rendergraph()` sets `m_is_registered = false` on
all live nodes and destroys the retired ones. `Rendergraph_node_deleter`
destroys a node whose flag is cleared directly; otherwise the rendergraph
unregisters it ("Node lifetime"). `Rendergraph_node::~Rendergraph_node()`
verifies the node is no longer registered.

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
topology changes. Registering, releasing, `connect()` and `disconnect()`
invalidate the cache; `prepare_frame()` sorts (under the graph mutex)
before it copies the list.

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

- `Render_target` handles MSAA resolve internally when `sample_count > 1`
  (a texture sample count of 0 or 1 is single-sample on every backend).
- The `none` key (0) in `Texture_rendergraph_node_create_info` means "do not
  register an output pin". Used for nodes that conditionally produce output.
- `get_graph()` exposes the internal `erhe::graph::Graph` (node / link
  topology) for the editor's rendergraph viewer window.
