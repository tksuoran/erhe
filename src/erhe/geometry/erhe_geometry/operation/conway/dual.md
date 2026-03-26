# Conway Dual

## Operation

The dual operator (Conway notation: **d**) swaps vertices and faces. Each
face becomes a vertex (at its centroid), and each vertex becomes a face
connecting the centroids of all incident faces.

## Topology

- Input: V vertices, E edges, F faces
- Output: F vertices, E edges, V faces
- Involutory: dual(dual(P)) returns to the original combinatorial structure

## Algorithm

1. Create a centroid vertex for each source face.
2. For each source vertex of valence N, create an N-gon face connecting
   the centroids of all N incident faces.
3. Interpolate attributes and recompute normals/connectivity.

## Parameters

None. The operation is purely combinatorial.
