# Conway Kis

## Operation

The kis operator (Conway notation: **k**) replaces each N-sided face with
N triangles by connecting each edge to a new apex vertex at the face centroid.
The result is always an all-triangles mesh. Kis is the dual of truncation
(k = dt) and produces the Kleetope of a polyhedron.

## Topology

- Input: V vertices, E edges, F faces
- Output: V + F vertices, sum of face corner counts faces (all triangles)

## Algorithm

1. Copy all source vertices to destination.
2. Create one centroid vertex per source face.
3. For each source face, for each edge (v_i, v_{i+1}), create a triangle
   (centroid, v_i, v_{i+1}).
4. Interpolate attributes and recompute normals/connectivity.

## Parameters

None currently. A `height` parameter (default 0.0) would offset the centroid
vertex along the face normal, producing raised (positive) or sunken (negative)
pyramids. At height=0, the result is a flat triangulation identical to the
current implementation.
