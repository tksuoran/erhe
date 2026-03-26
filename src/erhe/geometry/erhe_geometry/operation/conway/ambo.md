# Conway Ambo (Rectification)

## Operation

The ambo operator (Conway notation: **a**) performs rectification. It places
a new vertex at the midpoint of each edge, then creates two families of faces:
one per original vertex (connecting its edge midpoints) and one per original
face (connecting its edge midpoints).

## Topology

- Input: V vertices, E edges, F faces
- Output: E vertices, V + F faces, 2E edges
- Self-dual: ambo(P) = ambo(dual(P))

## Algorithm

1. Create one midpoint vertex per edge at t=0.5.
2. For each source vertex of valence N, create an N-gon from the midpoints
   of all edges meeting at that vertex.
3. For each source N-gon face, create an N-gon from the midpoints of its edges.
4. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. Could accept a `ratio` parameter (default 0.5) controlling
edge midpoint position. At ratio=1/3, the vertex-derived faces would
approximate truncation.
