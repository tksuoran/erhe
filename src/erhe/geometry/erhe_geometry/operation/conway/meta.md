# Conway Meta

## Operation

The meta operator (Conway notation: **m**) subdivides each N-sided face into
2N triangles by placing midpoints on every edge and connecting them through
the face centroid. Meta is equivalent to kis applied after join (m = kj).

## Topology

- Input: V vertices, E edges, F faces
- Output: V + F + E vertices, 2 * sum of face corner counts faces (all triangles)

## Algorithm

1. Copy all source vertices to destination.
2. Create one centroid vertex per source face.
3. Create one midpoint vertex per edge at t=0.5.
4. For each corner vertex V of each face, with predecessor A and successor C:
   - Create triangle A: (centroid, midpoint(A,V), V)
   - Create triangle B: (centroid, V, midpoint(V,C))
5. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A `height` parameter (default 0.0) would offset centroid
vertices along the face normal, same as for kis.
