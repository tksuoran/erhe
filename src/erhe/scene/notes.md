# erhe_scene

## Purpose
A glTF-like 3D scene graph providing hierarchical transforms, node attachments (meshes, cameras, lights, skins), animations, and scene management. Nodes form a parent-child tree with automatic world transform propagation. The library is graphics-API-agnostic and does not perform any rendering itself.

## Key Types
- `Scene` -- Top-level container owning the root node, flat node list, mesh layers, light layers, cameras, and skins. Provides `update_node_transforms()` and lookup by ID.
- `Node` -- Extends `Hierarchy` (parent/child tree). Holds `Node_transforms` (parent-from-node and world-from-node `Trs_transform`), attachments, and a `Scene_host` pointer. Supports cloning.
- `Node_attachment` -- Base class for things attached to nodes (Mesh, Camera, Light). Receives notifications on node transform changes and scene host changes.
- `Mesh` -- Node attachment holding a vector of `Mesh_primitive` (Primitive + Material pairs). Supports raytrace primitives for CPU-side picking.
- `Camera` -- Node attachment with a `Projection` (perspective/orthogonal/XR). Computes `clip_from_world` transforms.
- `Light` -- Node attachment for directional, point, and spot lights. Computes shadow projection transforms.
- `Projection` -- Camera projection configuration supporting many types (perspective vertical/horizontal, orthogonal, XR asymmetric, generic frustum).
- `Transform` -- Matrix + inverse matrix pair with factory methods for projection setups.
- `Trs_transform` -- Extends `Transform` with decomposed translation, rotation, scale, and skew. Supports interpolation.
- `Animation` / `Animation_sampler` / `Animation_channel` -- Keyframe animation system supporting step, linear, and cubic spline interpolation for translation, rotation, scale, and weights.
- `Skin` -- Skeletal skinning data (joint nodes + inverse bind matrices).
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
