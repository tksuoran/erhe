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
- `Animation` / `Animation_sampler` / `Animation_channel` -- Keyframe animation system supporting step, linear, and cubic spline interpolation for translation, rotation, scale, and weights. `Animation` is a typed prim (`erhe::Typed`, `src/erhe/item/notes.md` "Prim classes") with the fixed `typeName` token `Animation`, as `Skin` is with `Skin`.
- `Skin` -- Skeletal skinning data (joint nodes + inverse bind matrices, plus the optional glTF `skeleton` pivot node). `get_skin_transform_root()` returns the node an editor should transform to move a skinned mesh: skinning ignores the mesh node's own transform (glTF 2.0 requires it), so only a common ancestor of the joints moves the posed result. Uses `Skin_data::skeleton` when set, else the closest common ancestor of the joints. One `Skin` is shared by every `Mesh` it skins, and each of those meshes registers it with the scene, so `Scene::register_skin()` / `Scene::unregister_skin()` count the uses: `get_skins()` holds one entry per skin, the entry appears with the first skinned mesh and leaves with the last, and the returned `Skin_registry_change` tells a caller that announces the change (`Scene_root` publishes `Skin_registered_message`) to announce it once.
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
  an extent, and holds nothing until a step moves those values here.
  **`Gprim`** (USD `UsdGeomGprim`, base `Boundable`) is the level that draws
  geometry, and holds `double_sided` (USD `doubleSided`): an entry-store
  property, default false, that inherits, so a holding prim or a style can
  carry `Gprim.double_sided` for the geometry below it. `displayColor` belongs
  to this level too and is not held yet. **`Mesh`** (USD `Mesh`, base `Gprim`)
  is the geometric prim erhe draws.
- `erhe::scene::is_double_sided(mesh, mesh_primitive)` (`mesh.hpp`) is the one
  place the double-sided rule is spelled, so no two render passes can
  disagree: a primitive is drawn from both sides when its material asks for it
  (`Material.double_sided`, glTF `material.doubleSided`) or when the prim
  itself does (`Gprim.double_sided`). Both draw-list classification
  (`draw_list_scene.cpp`) and bucket classification (`mesh_memory.cpp`) ask it,
  and a `double_sided` write reaches them through
  `Mesh::notify_primitives_changed()`, which re-registers the mesh's draw list
  entries - nothing polls the flag per frame.

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

## Authored xformOp stacks

A prim may carry the USD xformOp stack it was authored with next to the single
`Trs_transform` it composes to (`doc/usd-compatibility-plan.md` M8), so an
imported stack round-trips as authored instead of collapsing to one
`xformOp:transform` matrix. `Xform_op` is one `xformOp:<type>[:<suffix>]` -
its type from USD's vocabulary (`translate`, `scale`, the single-axis and
three-angle rotates, `orient`, `transform`), the authored precision it is
written back as, its suffix, its `!invert!` flag, and its value in double
precision whatever the authored precision was. The value of a three-angle
rotate holds the x, y and z angles in degrees; the type names the order they
are applied to a point in. `Xform_op_stack` is the ordered `xformOpOrder` plus
the `!resetXformStack!` flag, which is stored and round-tripped and nothing
else: erhe's transform propagation always composes with the parent, so
`compose()` ignores it.

`Xform_op_stack::compose()` returns the glm column-vector matrix
`M(op0) * M(op1) * ... * M(opN)`, so a point is transformed by the last op
first and a `[translate, rotate, scale]` stack composes to `T * R * S`.

An op may also carry the time samples the file authored for it
(`Xform_op::samples`), in the authoring file's own time codes and in the op's
own value form; `Xform_op::value` stays the op's single value, so `compose()`
and the write-back need no notion of time. The samples are the authored record
a save writes back, and their playable projection is an `Animation` built by
the reader that produced them (`src/erhe/usd/notes.md`, "Time samples"); a
write-back changes `value` alone and leaves the samples as authored.

A prim without a stack - the common case - carries a null pointer and nothing
else. While a stack is present it is the authoritative form of the local
transform: `Xformable::handle_local_transform_written`, the shared tail of
every local transform write (the `set_parent_from_node` / `set_node_from_parent`
/ `set_world_from_node` / `set_node_from_world` family and the bridged
translation / rotation / scale properties), writes the new TRS back into the
stack and then sets `parent_from_node` to the stack's composition, so a
rotation that goes through an Euler op comes back as that op composes it. The
write-back
(`write_trs_into_xform_op_stack`) sends the translation to the last
non-inverted `translate` op with no suffix, the rotation to the last
non-inverted `orient` or `rotate_*` op with no suffix (converted to that op's
form), and the scale to the last non-inverted `scale` op with no suffix; a
stack that is one plain `transform` op takes the whole matrix; every other op
keeps its value. Only components that changed are written, and the stack must
compose to the requested transform after the write. A component that changed
and has no designated op - no `scale` op and the scale changed, a `rotate_x`
op and the new rotation is not about x - or a composition that the ops which
were not written move away from the requested transform - a pivot pair around
the rotate op takes a rotation edit somewhere else - makes the stack unable to
carry the edit: the ops keep their authored values, and the whole stack is
replaced by one `transform` op holding the matrix, logged once per prim at
info level. So a prim always ends up exactly where the edit put it, whatever
its authored stack looks like.

Undo restores a recorded transform and its recorded stack verbatim through
`Xformable::restore_local_transform`, which runs no write-back, so a stack a
collapse replaced comes back whole; `Node_transform_operation` records the
stack next to the matrices.

glTF does not carry the stack (`doc/usd-compatibility-plan.md` C1): a glTF
save writes the composed TRS, and a glTF scene has no stack to start with.

## Instance overrides

`instance_override.hpp` owns what an override of a prefab instance item is
(`doc/usd-compatibility-plan.md` X2, `doc/property-system.md` D33): a local
value of a serializable, non-bridged, non-computed property without an
expression, a local transform that differs from the template counterpart's, or
a material bound to a primitive of the item's mesh that differs from the one
the counterpart's primitive at the same index binds. The name is structure and
is never an override. The walk starts at the
referencing item (the carrier): its children are the arcs' clones of the
target prims, and every item below one that names a counterpart
(`Dependency_object::get_reference`) is instance content - an item that names
none was parented under the carrier by hand and is reported by neither
collector.

`collect_instance_override_items` reports the items, for a writer that reads
the values in its own file format (`erhe::usd`);
`collect_instance_overrides` reports the same set with the values read out as
qualified name / D16 text pairs, the transform as a matrix plus the authored
xformOp stack, and a material binding as the path of the material item, so it
survives the items it came from - that form is what `erhe::gltf` writes, what
a file reader produces, and what `apply_instance_overrides` puts back on a
freshly attached instance.

A binding that covers one group of facets rather than the whole mesh is an
entry of its own whose relative path ends in the name of the group, the way a
USD GeomSubset is a prim below its mesh; the group of a primitive is named by
the primitive's geometry, which the importer names `<mesh name>.<subset
name>`. Applying a binding resolves its path below the carrier first (a path
a file authors starts at the arc's target prim, which is the clone the carrier
holds) and then from the carrier's ancestors, with the extra level an instance
keeps treated as transparent: USD composes an arc's content directly under the
referencing prim, while erhe keeps the target clone as a level of its own
(`doc/usd-compatibility-plan.md` X1).

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
