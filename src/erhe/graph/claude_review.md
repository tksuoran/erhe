# erhe_graph -- Code Review

## Summary
A DAG (directed acyclic graph) framework with nodes, pins, and links, used as the basis for the render graph system. The implementation is clean and functional with proper topological sort. Main concerns are around the O(n^2) sort algorithm and exposed internal state.

## Strengths
- Clean abstractions: `Graph` owns `Node*` and `Link` (unique_ptr), `Pin` references `Link*`
- Topological sort correctly detects cycles and reports errors
- `make_graph_id()` uses `std::atomic<int>` for thread-safe unique ID generation
- Proper cleanup in `unregister_node()` -- disconnects all links before removing
- Key-based pin matching in `connect()` prevents incompatible connections

## Issues
- **[moderate]** `Graph::sort()` (graph.cpp:91-152) is O(n^2) -- it repeatedly scans the unsorted list and checks dependencies against the sorted list. For the typical render graph size (10-50 nodes) this is fine, but it also modifies `unsorted_nodes` during iteration by erasing and breaking. The algorithm could be cleaner using Kahn's algorithm with an in-degree map.
- **[moderate]** `Graph` exposes its internal members publicly (graph.hpp:40-42): `m_nodes`, `m_links`, `m_is_sorted`. These should be private. The `get_nodes()` and `get_links()` accessors already exist.
- **[moderate]** `unregister_node()` (graph.cpp:38-47) iterates `pin.get_links()` while calling `disconnect()` which modifies the link list. This is a potential iterator invalidation bug. The links should be collected first, then disconnected.
- **[minor]** `Link` has a virtual destructor (link.hpp:17) but is stored in `std::unique_ptr<Link>` and never subclassed. The virtual destructor adds unnecessary overhead.
- **[minor]** `Pin` has a virtual destructor (pin.hpp:22) but is stored by value in vectors. Virtual dispatch on value-type objects stored in vectors is unusual.
- **[minor]** `Node` uses CRTP (`Item<..., Node, ...>`) which ties it to the item system. The graph library would be more reusable if decoupled from `erhe::Item`.
- **[minor]** `SPDLOG_LOGGER_TRACE` call on graph.cpp:126 includes `node->get_id()` in the format args but the format string only has one `{}` placeholder for the name.

## Suggestions
- Make `m_nodes`, `m_links`, and `m_is_sorted` private.
- Fix the iterator invalidation in `unregister_node()` by collecting links to disconnect before iterating.
- Consider implementing Kahn's algorithm for topological sort -- it's simpler, handles cycle detection naturally, and is O(V+E).
- Remove virtual destructors from `Link` and `Pin` unless subclassing is planned.
- Consider decoupling `Node` from `erhe::Item` to make the graph library independently reusable.
- Set `m_is_sorted = false` when nodes or links are added/removed (currently only `sort()` sets it to true, but there's no invalidation).
