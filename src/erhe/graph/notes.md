# erhe_graph

## Purpose
Generic directed acyclic graph (DAG) framework. Nodes have typed input and output pins
that can be connected via links. The graph can be topologically sorted for dependency-ordered
execution. Used as the base for the render graph system.

## Key Types
- `Graph` -- Container of nodes and links. Provides `connect()`, `disconnect()`, `sort()`, and node registration. Extends `erhe::Item_host`.
- `Node` -- Graph node with input/output pins. Extends `erhe::Item`. Has a unique graph ID.
- `Pin` -- A connection point on a node, either source (output) or sink (input). Identified by key and slot index.
- `Link` -- A directed connection from a source pin to a sink pin. Has a unique ID.

## Public API
- `graph.register_node(node)` / `graph.unregister_node(node)` -- Add/remove nodes.
- `graph.connect(source_pin, sink_pin)` -- Create a link between pins, returns `Link*`.
- `graph.disconnect(link)` -- Remove a link.
- `graph.sort()` -- Topological sort of nodes.
- `node.base_make_input_pin(key, name)` / `base_make_output_pin(key, name)` -- Create pins on a node.
- `pin.get_links()` -- Get all links connected to a pin.

## Dependencies
- **erhe libraries:** `erhe::item` (private), `erhe::defer` (private), `erhe::log` (private), `erhe::verify` (private)
- **External:** None

## Notes
- `Node` inherits from `erhe::Item` and uses the item type `graph_node`.
- Each node and link gets a unique auto-incremented ID via `make_graph_id()`.
- The `sort()` method enables dependency-ordered traversal (used by render graph).
- Pins use a key (user-defined semantic) and slot (index within input/output list).
