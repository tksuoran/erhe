# erhe_scene

## Purpose
A glTF-like 3D scene graph providing hierarchical transforms, prim classes (see "Prim levels"), node attachments (physics, layout, grid, ...), animations, and scene management. Nodes form a parent-child tree with automatic world transform propagation. The library is graphics-API-agnostic and does not perform any rendering itself.

## Key Types
- `Scene` -- Top-level container owning the root node, flat node list, mesh layers, light layers, cameras, and skins. Provides `update_node_transforms()` and lookup by ID.
- `Xformable` (`Node`) -- The transform level of the prim class hierarchy, see "Prim levels". Holds `Node_transforms` (parent-from-node and world-from-node `Trs_transform`), attachments, and a `Scene_host` pointer. Supports cloning. Registers `translation`, `rotation` and `scale` as erhe::property properties bridged onto the parent-from-node `Trs_transform` (`Node::translation_property` etc.; writes run the same world-transform update as `set_parent_from_node`), so the editor's generic property rows, undo and MCP reach the transform without a second copy of it (doc/property-system.md section 4.2). `world_translation_property` / `world_rotation_property` / `world_scale_property` are computed properties (D26) reading `world_from_node_transform()`; `handle_transform_update` pushes them to expressions, so a descendant's values follow a parent move when `Scene::update_node_transforms` recomputes it.
- `Imageable` / `Xform` / `Boundable` / `Gprim` -- The other prim levels this library owns, see "Prim levels".
- `Node_attachment` -- Base class for what USD applies to a prim as an API schema and erhe attaches to a node: `Node_physics`, `Node_joint`, `Layout`, `Brush_placement`, `Prefab_instance`, `Frame_controller` and `Grid`. `Mesh`, `Camera` and `Light` are prims, not attachments. Receives notifications on node transform changes and scene host changes. Its property inheritance parent is its node (`visible`, `shadow_cast`, `lightmapped` flow node -> attachment; `set_node` brackets the move with the inheritance snapshot and keeps the attachment alive while the old node's list releases it).
- `Mesh_primitive` -- Primitive + Material pair, a `Dependency_object` with its own owner type: `material` is an object property (`Mesh_primitive::material_property`, `register_member` over the member, `doc/property-system.md` D28 / section 4.9) whose `after_set` notifies the owning mesh's scene host; `Mesh::set_primitive_material` writes it and stays the one writer. Carries an owner link (mesh, index) the mesh stamps after every primitive-list change. Registers member-backed properties only and is never observed: the mesh holds primitives by value, and a vector reallocation copy-constructs the base.
- `Mesh` -- A geometric prim (`Gprim`, see "Prim levels") holding a vector of `Mesh_primitive`: an `Xformable` with its own transform, name and children, a child prim of its parent, and a parent holds any number of them. Its override of the item-host hook registers it with the scene's mesh layers on top of the node registration the base does, and its override of `handle_transform_update` mirrors the world transform into the raytrace instances, the negative-determinant flag and the computed world bounds. Addresses its primitives as property sub-objects (D29: `get_property_sub_object_count` / `get_property_sub_object` / `get_property_sub_object_label`). Supports raytrace primitives for CPU-side picking. `get_aabb_world()` returns POSED world bounds for a skinned mesh: it unions the primitives' per-joint rest boxes (`Buffer_mesh::joint_bounding_boxes`) transformed by `world_from_bind` (`get_skinned_aabb_world()`), and does NOT apply the mesh's own transform, which skinning ignores. Correct because a skinned position is a convex combination of its per-joint images, so it lies inside the union. Uncached - joints move every frame and primitives can be rebuilt behind the Mesh's back, so there is no reliable invalidation signal. `world_bounds_min_property` / `world_bounds_max_property` are computed properties (D26) reading `get_aabb_world()` (zero for an invalid box), pushed to expressions from `handle_transform_update` and the primitive changes.
- `Camera` -- A transformable prim (`Xformable`, see "Prim levels") with a `Projection` (perspective/orthogonal/XR): a child prim of its parent with its own transform, and a parent holds any number of them. Its override of the item-host hook registers it with the scene's camera list on top of the node registration the base does. Computes `clip_from_world` transforms. The `Projection` fields are registered as bridged `erhe::property` properties (`Camera::z_far_property`, ...), so `projection()` writes and property writes reach the same state; exposure and shadow range live in the property store (`doc/property-system.md` section 4.4).
- `Light` -- A transformable prim (`Xformable`, see "Prim levels") for directional, point and spot lights: a child prim of its parent with its own transform, and a parent holds any number of them. `light_type` picks the UsdLux schema the USD writer emits; one class per schema arrives when a light type needs properties of its own. Its override of the item-host hook registers it with the scene's light layer on top of the node registration the base does. Computes shadow projection transforms. The authored state (`light_type`, `color`, `intensity`, `temperature`, `range`, spot angles, `cast_shadow`) is registered `erhe::property` properties read through `get_color()`-style accessors; every change re-resolves the scene light set through the shared changed callback (`Scene_host::on_light_changed`), so no writer notifies by hand (`doc/property-system.md` section 4.3, D19).
- `Layout` -- Node attachment that owns a volume (an `Aabb` in the node's local space) and arranges its node's direct children inside that volume by computing each child's `parent_from_node` (`Layout::update()`). A single class selects between `Layout_type::stack` (one signed axis), `grid` (an X/Y/Z cell grid), and `flow` (children wrapped into lines along the primary axis, lines into sheets along the secondary axis, sheets stacked along the tertiary axis). The layout owns each child's translation and (for `stretch` alignment) scale; child rotation is forced to identity. A child's footprint is measured via `compute_content_local_aabb()` (its own mesh primitives plus descendants); a child that is itself a `Layout` contributes its declared volume instead, which both matches intent and breaks the recursion cycle. The parameters (type, volume, axes, gap, grid track count) are registered `erhe::property` properties (doc/property-system.md section 4.13) behind typed accessors; the grid track extent lists are not.
- Per-child layout hints (alignment `negative`/`positive`/`stretch` per axis, margins, grid cell/span) are attached properties registered by `Layout` and set on the child `Node` (`Layout.align_x` .. `Layout.grid_span`, doc/property-system.md section 4.14); a child without local values is laid out using the defaults.
- `Projection` -- Camera projection configuration supporting many types (perspective vertical/horizontal, orthogonal, XR asymmetric, generic frustum).
- `Transform` -- Matrix + inverse matrix pair with factory methods for projection setups.
- `Trs_transform` -- Extends `Transform` with decomposed translation, rotation, scale, and skew. Supports interpolation.
- `Animation` / `Animation_sampler` / `Animation_channel` -- Keyframe animation system supporting step, linear, and cubic spline interpolation for translation, rotation, scale, and weights.
- `Skin` -- Skeletal skinning data (joint nodes + inverse bind matrices, plus the optional glTF `skeleton` pivot node). `get_skin_transform_root()` returns the node an editor should transform to move a skinned mesh: skinning ignores the mesh node's own transform (glTF 2.0 requires it), so only a common ancestor of the joints moves the posed result. Uses `Skin_data::skeleton` when set, else the closest common ancestor of the joints.
- `Mesh_layer` / `Light_layer` -- Organize meshes and lights into layers with flags and IDs.
- `Scene_host` -- Abstract interface for registering/unregistering scene objects.

## Prim levels

Every scene is one tree of prims, and the erhe class of a prim sits in a class
hierarchy that mirrors the USD schema hierarchy
(`doc/usd-compatibility-plan.md` C5); `erhe::item` owns the levels that need
no transform (`Typed`, `Scope`, see `src/erhe/item/notes.md`) and this library
owns the rest:

- **`Imageable`** (USD `UsdGeomImageable`, base `erhe::Typed`) is the level
  that renders, so it is the level `visible` and `purpose` belong to; both
  are registered on `Item_base` until a step moves them here.
- **`Xformable`** (USD `UsdGeomXformable`, base `Imageable`) is the level that
  carries a transform. `Node` is an alias of it, and the name most of erhe
  still spells it with; the alias is retired when the prim class hierarchy is
  complete.
- **`Xform`** (USD `Xform`, base `Xformable`) is a transform with children and
  nothing else. It is the class every node-creation path makes - the Create
  menu, the MCP `create_node` tool, the import of a transform-only node - so
  `Xformable` itself is never instantiated.
- **`Boundable`** (USD `UsdGeomBoundable`, base `Xformable`) is the level with
  an extent, and **`Gprim`** (USD `UsdGeomGprim`, base `Boundable`) the level
  that draws geometry, so `doubleSided` and `displayColor` belong to it. Both
  hold nothing until a step moves those values here. **`Mesh`** (USD `Mesh`,
  base `Gprim`) is the geometric prim erhe draws.

Any prim may parent any other prim, so the rules that walk the tree take it as
the tree of prims it is:

- `Xformable::get_parent_node()` returns the nearest `Xformable` ancestor, not
  the parent: a prim outside `Xformable` has no transform, so a transform
  composes with the first `Xformable` above it and passes through the prims
  that have none. Every reader of a parent transform goes through it. It walks
  rather than caching the ancestor, because a cache would have to be
  invalidated through the whole subtree on every reparent of an ancestor; the
  walk steps only over transformless prims, so on a tree of nodes it is the
  single hop the cast was.
- `Scene::update_subtree_transforms()` recurses THROUGH a prim that has no
  transform, so an `Xform` under a `Scope` follows its ancestor's move.
- `Xformable::handle_item_host_update()` registers the node with the scene host
  and carries the host to its attachments and to every prim child, `Scope`
  children included; `erhe::Typed` owns the hook and the parent-update rule
  that drives it (see `src/erhe/item/notes.md`), so a `Scope` attached under a
  hosted prim registers every `Xformable` in its subtree with the scene, and
  detaching it unregisters them.

A level has its own `Item_type` bit and a concrete class's static type is the
OR of its chain (`Xform::get_static_type()` is
`typed | imageable | xformable | xform`), so `is<Xformable>(xform)` holds by
the ordinary subset test and the property owner-type chain
(`doc/property-system.md` D27) follows the same levels: a node's registered
properties sit on `Xformable`, under `Imageable`, under `Typed`. A level below
`Xformable` clones through its `(src, for_clone)` constructor, because the
transform level owns attachments and a scene host that a plain copy does not
reproduce.

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
- `get_mesh(item)` answers the one-mesh case: the item when it is a `Mesh`, else its first `Mesh` child. `get_camera(item)` and `get_light(item)` answer the same question for a `Camera` and a `Light`. `for_each_mesh_child(item, callback)` visits every `Mesh` child and allocates nothing, so per-frame code can use it. `set_prim_parent(prim, parent)` makes any `Xformable` a child prim keeping its LOCAL transform - `Xformable::set_parent` preserves the WORLD transform, which would give a prim created at the origin a local transform cancelling its new parent's; `set_mesh_parent(mesh, parent)` is the name the mesh call sites spell it with.
- Mesh layers use a `Layer_id` (uint64) and flag bits for filtering during rendering.
