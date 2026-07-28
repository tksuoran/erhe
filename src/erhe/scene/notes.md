# erhe_scene

## Purpose
A glTF-like 3D scene graph providing hierarchical transforms, node attachments (meshes, cameras, lights, skins), animations, and scene management. Nodes form a parent-child tree with automatic world transform propagation. The library is graphics-API-agnostic and does not perform any rendering itself.

## Key Types
- `Scene` -- Top-level container owning the root node, flat node list, mesh layers, light layers, cameras, and skins. Provides `update_node_transforms()` and lookup by ID.
- `Node` -- Extends `Hierarchy` (parent/child tree). Holds `Node_transforms` (parent-from-node and world-from-node `Trs_transform`), attachments, and a `Scene_host` pointer. Supports cloning.
- `Node_attachment` -- Base class for things attached to nodes (Mesh, Camera, Light). Receives notifications on node transform changes and scene host changes.
- `Mesh` -- Node attachment holding a vector of `Mesh_primitive` (Primitive + Material pairs). Supports raytrace primitives for CPU-side picking. `get_aabb_world()` returns POSED world bounds for a skinned mesh: it unions the primitives' per-joint rest boxes (`Buffer_mesh::joint_bounding_boxes`) transformed by `world_from_bind` (`get_skinned_aabb_world()`), and does NOT apply the mesh node's transform, which skinning ignores. Correct because a skinned position is a convex combination of its per-joint images, so it lies inside the union. Uncached - joints move every frame and primitives can be rebuilt behind the Mesh's back, so there is no reliable invalidation signal.
- `Camera` -- Node attachment with a `Projection` (perspective/orthogonal/XR). Computes `clip_from_world` transforms.
- `Light` -- Node attachment for directional, point, and spot lights. Computes shadow projection transforms.
- `Layout` -- Node attachment that owns a volume (an `Aabb` in the node's local space) and arranges its node's direct children inside that volume by computing each child's `parent_from_node` (`Layout::update()`). A single class selects between `Layout_type::stack` (one signed axis), `grid` (an X/Y/Z cell grid), and `flow` (children wrapped into lines along the primary axis, lines into sheets along the secondary axis, sheets stacked along the tertiary axis). The layout owns each child's translation and (for `stretch` alignment) scale; child rotation is forced to identity. A child's footprint is measured via `compute_content_local_aabb()` (its own mesh primitives plus descendants); a child that is itself a `Layout` contributes its declared `volume` instead, which both matches intent and breaks the recursion cycle.
- `Layout_item` -- Optional per-child node attachment holding alignment (`negative`/`positive`/`stretch` per axis), margins, and grid cell/span. A child without one is laid out using default values.
- `Projection` -- Camera projection configuration supporting many types (perspective vertical/horizontal, orthogonal, XR asymmetric, generic frustum).
- `Transform` -- Matrix + inverse matrix pair with factory methods for projection setups.
- `Trs_transform` -- Extends `Transform` with decomposed translation, rotation, scale, and skew. Supports interpolation.
- `Animation` / `Animation_sampler` / `Animation_channel` -- Keyframe animation system supporting step, linear, and cubic spline interpolation for translation, rotation, scale, and weights.
- `Skin` -- Skeletal skinning data (joint nodes + inverse bind matrices, plus the optional glTF `skeleton` pivot node). `get_skin_transform_root()` returns the node an editor should transform to move a skinned mesh: skinning ignores the mesh node's own transform (glTF 2.0 requires it), so only a common ancestor of the joints moves the posed result. Uses `Skin_data::skeleton` when set, else the closest common ancestor of the joints.
- `Mesh_layer` / `Light_layer` -- Organize meshes and lights into layers with flags and IDs.
- `Scene_host` -- Abstract interface for registering/unregistering scene objects.

## Public API
- Create a `Scene`, add nodes with `register_node()`, attach meshes/cameras/lights.
- Call `scene.update_node_transforms()` each frame to propagate world transforms.
- Use `Node::set_parent_from_node()` / `set_world_from_node()` to position nodes.
- `Camera::projection_transforms(viewport)` returns clip-from-world matrices.
- `Animation::apply(time)` drives node transforms from keyframe data.

## Dependencies
- erhe::item (Item, Hierarchy, Unique_id)
- erhe::primitive (Primitive, Material)
- erhe::raytrace (IGeometry, IInstance, IScene -- for CPU raytrace picking)
- erhe::math (Viewport, Aabb)
- glm

## Notes
- Transform updates use a global serial number to avoid redundant recomputation.
- `get_attachment<T>(node)` is a convenience template for finding typed attachments.
- Mesh layers use a `Layer_id` (uint64) and flag bits for filtering during rendering.
