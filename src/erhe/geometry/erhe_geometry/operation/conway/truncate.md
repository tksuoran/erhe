# Conway Truncate

## Operation

The truncate operator (Conway notation: **t**) cuts off every vertex,
replacing each vertex of valence V with a V-sided polygon. Each original
N-gon face becomes a 2N-gon. Truncation is the dual of kis (t = dk).

## Topology

- Input: V vertices, E edges, F faces
- Output: 2E vertices, V + F faces, 3E edges
- Each original N-gon face becomes a 2N-gon
- Each vertex of valence V becomes a V-gon

## Algorithm

1. Trisect each edge at t=1/3 and t=2/3, creating two new vertices per edge.
2. For each source vertex, create a polygon connecting the nearer trisection
   point on each incident edge.
3. For each source N-gon face, create a 2N-gon using both trisection points
   from each edge.
4. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A `ratio` parameter (default 1/3, range (0, 0.5]) would
control how deeply vertices are cut:
- ratio=1/3: standard truncation
- ratio=0.5: equivalent to ambo (rectification)
- ratio near 0: approaches the original polyhedron
