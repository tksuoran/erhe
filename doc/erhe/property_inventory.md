# Property inventory

Stability: stable

The status of every editor-visible item field with respect to the
property system (`erhe::property`, design record `doc/erhe/property_system.md`):
which fields are registered properties, how each is stored, and which
fields the Properties window still draws by hand. This document is the
owner of that status; the design record's sections 4.1 to 4.14 own the
design of each migration and refer here for the per-field list.

Update this document in the same commit as any registration added,
removed or changed in storage kind, and whenever a hand-written row is
migrated or added. The same commit updates the owner's subsection in
`doc/erhe/property_system.md` (4.1 to 4.14) when the design changed, and
`doc/erhe/property.md` when a library mechanism changed; the design
record's "Document roles" paragraph states the split.

## Storage kinds

- **entry** - the value lives in the object's entry store
  (`Dependency_object`), with the default, style, inherited and
  expression layers (D5).
- **member** - member-backed through `Property<T>::register_member`
  (D18): the object's member is the storage, the property reads and
  writes it, and an `after_set` hook runs the consequence. Always reports
  `Value_source::local`.
- **bridge** - member-backed through a hand-written `Property_bridge`
  (D18), used where the value is not a plain member or the set has a side
  effect the member write does not cover. Does its own no-op check.
- **computed** - `register_computed` (D26): read-only, or writable
  through a setter that writes a stored property (the `writes` note).
  An entry-stored property may instead take a per-object DEFAULT layer
  (`Property_metadata::compute_default`, D31) and keep every layer above
  it.
- **attached** - `register_attached` (D3): registered by one type, set on
  objects of another, listed by the D12 rule under its qualified
  `<owner>.<name>`; any item can take one through the Properties
  window's Add Property picker and drop it again with Remove Property
  (D12). Layout's per-child hints (section 4.14).

## Registration flags

`Property_flags::native_gltf` (D32) marks a registration whose value the
glTF exporter writes through a native glTF field or a typed `ERHE_*`
extension field whenever it differs from the property's default, so a
property serializer writes no value entry of its own for it. It carries no
behavior; these are the registrations that have it:

- `Light`: light_type, color, intensity, range, inner_spot_angle,
  outer_spot_angle, cast_shadow (temperature has no glTF carrier).
- `Camera`: every projection field, infinite_z_far, exposure, shadow_range
  - `ERHE_camera` writes the complete projection on every export.
- `Material`: base_color, opacity, roughness, metallic, emissive, ior,
  transmission, normalmap_encoding, bxdf_model, blending_mode,
  double_sided, use_circular_brushed_metal, use_aniso_control and the five
  texture slots. The fields whose carrier is conditional on something
  other than the value itself are deliberately absent: alpha_cutoff (only
  in the MASK alpha mode), normal_texture_scale and
  occlusion_texture_strength (only with that slot's texture bound),
  circular_brushed_metal_texgen_mode (only with the brushed metal block
  on), the per-slot texgen / UV transform / sampler fields (only with the
  slot bound), and reflectance (no glTF carrier at all).
- `Mesh_primitive`: material (the glTF primitive's material index).
- `Collision_filter`: collision_systems, collide_with_systems,
  not_collide_with_systems - the `KHR_physics_rigid_bodies`
  `collisionFilters` entry states all three, and the USD
  `PhysicsCollisionGroup` prim carries them under the same attribute
  names a resource prim's local value would use.
- `Physics_joint_settings`: all 66 per-axis limit and drive properties - the
  `KHR_physics_rigid_bodies` `physicsJoints` entry states them as its limits
  and drives, and the USD `PhysicsLimitAPI` / `PhysicsDriveAPI` instances
  spell the same axes.

The Properties window tints a row's label by its value source (D12):
member and bridge rows are blue, entry rows green / gray / cyan / orange /
purple by layer, computed rows dim gray. Untinted rows are hand-written.

## Registered properties

### Item_base (`src/erhe/item/erhe_item/item.cpp`)

| Property | Storage | Notes |
|---|---|---|
| visible | entry | flag mirror |
| purpose | entry | USD purpose enumeration, `inherits`; its default layer is per-object (D31), derived from the editor-only flag bits (`doc/erhe/item.md` "Purpose") |
| style | bridge | object reference to the item's style source (doc/editor/style_library.md D3), style items only |
| name | bridge | over `get_name` / `set_name` |
| tags | bridge | the tag set as one comma-separated string (`tags_to_string` / `tags_from_string`) |
| lock_viewport_transform, lock_edit, lock_viewport_selection | bridge | flag bits, "Locks" group; lock_edit is `Property_flags::writable_when_sealed` (D24) so the seal lifts through it |
| show_in_ui, show_debug_visualizations | bridge | flag bits |
| exclude_from_prefab, no_message, no_transform_update, transform_world_normative, show_in_developer_ui, ik_lock | bridge | flag bits, developer-only rows |

### Hierarchy (`src/erhe/item/erhe_item/hierarchy.cpp`)

| Property | Storage | Notes |
|---|---|---|
| child_count | computed | |

### Node (`src/erhe/scene/erhe_scene/node.cpp`, section 4.2)

| Property | Storage | Notes |
|---|---|---|
| translation, rotation, scale | bridge | over `Trs_transform`, no matrix round trip |
| world_translation, world_rotation, world_scale | computed | |
| lock_translation_x/y/z, lock_rotation_x/y/z, lock_scale_x/y/z | bridge | flag bits (`Item_flags::lock_*`) over `Item_base::register_flag_bit_property`, "Channel Locks" group; nodes only |

### Mesh and Mesh_primitive (`src/erhe/scene/erhe_scene/mesh.cpp`, sections 4.9, D29)

| Owner | Property | Storage | Notes |
|---|---|---|---|
| Mesh | world_bounds_min, world_bounds_max | computed | |
| Mesh | shadow_cast, lightmapped | entry | inherits (a node or a style holds Mesh.shadow_cast, D30); flag mirrors, group Rendering |
| Mesh_primitive (sub-object) | material | member | object reference, Material |

### Material (`src/erhe/primitive/erhe_primitive/material.cpp`, section 4.1)

| Property | Storage | Notes |
|---|---|---|
| base_color, opacity, roughness, metallic, reflectance, emissive, ior, transmission, normal_texture_scale, occlusion_texture_strength, alpha_cutoff | entry | scalars and colors; inherits (D30, from a content-library folder) |
| normal_texture_decode_scale, normal_texture_decode_bias | entry | vec4; the normal texture's texel decode (`texel * scale + bias`); inherits |
| normalmap_encoding, bxdf_model, blending_mode, circular_brushed_metal_texgen_mode | entry | enumerations; inherits |
| double_sided, use_circular_brushed_metal, use_aniso_control | entry | booleans; inherits |
| base_color_texture, metallic_roughness_texture, normal_texture, occlusion_texture, emissive_texture | entry | object references, texture or graph texture; inherits; mirrored into `Material_data` by `on_property_changed` |
| `<slot>`_texture_texgen_mode (5) | entry | enumeration, affects shader variant; inherits; mirrored |
| `<slot>`_texture_uv_rotation, _uv_offset, _uv_scale (15) | entry | inherits; mirrored |
| `<slot>`_texture_wrap_u, _wrap_v, _min_filter, _mag_filter, _mipmap_mode, _max_anisotropy, _lod_bias (35) | entry | the slot sampler state; inherits; mirrored into `Material_sampler_state`, resolved to a GPU sampler by the renderer; lod_bias developer-only |

### Light (`src/erhe/scene/erhe_scene/light.cpp`, section 4.3)

| Property | Storage | Notes |
|---|---|---|
| light_type, color, intensity, temperature, range, inner_spot_angle, outer_spot_angle, cast_shadow | entry | inherits (D30, from the node chain); shared property_changed re-resolves the light set |
| flux | computed | writes intensity (intensity times the emission solid angle); point and spot lights |
| blackbody | computed | read-only chromaticity of temperature; shown while temperature is positive |

### Scene (`src/erhe/scene/erhe_scene/scene.cpp`, section 4.20)

| Property | Storage | Notes |
|---|---|---|
| ambient_light | entry | does not inherit (a scene has no holder above it); mirrored into `m_ambient_light` by `on_property_changed`; color row |

### Camera (`src/erhe/scene/erhe_scene/camera.cpp`, section 4.4)

| Property | Storage | Notes |
|---|---|---|
| projection_type, infinite_z_far | entry | inherits (D30); mirrored into `Projection` by `on_property_changed` |
| fov_x, fov_y, fov_left, fov_right, fov_up, fov_down | entry | inherits; mirrored; angle rows |
| ortho_left, ortho_width, ortho_bottom, ortho_height, frustum_left, frustum_right, frustum_bottom, frustum_top, perspective_z_near, perspective_z_far | entry | inherits; mirrored; logarithmic extents |
| orthographic_z_near, orthographic_z_far | entry | inherits; mirrored; signed (the near plane may lie behind the camera) |
| exposure, shadow_range | entry | inherits |

### Node_physics (`src/editor/scene/node_physics.cpp`, section 4.26)

Attached properties of `erhe::scene::Node`, UI group `Rigid Body`, keyed on
`motion_mode`. The runtime state they imply lives in `Node_physics_system`.

| Property | Storage | Notes |
|---|---|---|
| motion_mode | entry | KEY property; enumeration, default `e_none` (no body); does not inherit |
| is_trigger | entry | inherits (D30); a sensor body, recreates the body |
| gravity_factor | entry | inherits; pushed to the live body; visible while the mode is movable |
| initial_linear_velocity, initial_angular_velocity | entry | inherits; world space, applied at (re)creation; visible while the mode is movable |
| mass | entry | inherits; source default = shape mass scaled by the material density, else scales the body's inertia; visible while the mode is movable |
| center_of_mass_offset | entry | inherits; realized as the collision shape wrapper, recreates the body |
| physics_material, collision_filter | entry | inherits; object references, a holder assigns them to every body below |
| collision_mesh | entry | weak object reference (D28) to the `Mesh` prim the shape was built from, none = the body's own mesh; does not inherit |

### Grid (`src/editor/grid/grid.cpp`, section 4.11)

| Property | Storage | Notes |
|---|---|---|
| plane_type, center, rotation | entry | inherits (D30, through the grid's style); on_property_changed re-derives the transform |
| frame_node | entry | weak object reference (D28) to the node the Node plane follows; does not inherit, not serialized (session state); re-takes the transform observer token on change |
| intersect_enable, snap_enabled, cell_size, cell_div, cell_count | entry | inherits |
| level0_color .. level3_color, level0_width .. level3_width | entry | inherits; the two mirror arrays follow |
| label_enable, label_text_fraction, label_spacing, label_fade, label_color | entry | inherits |

The members are a mirror of the effective values; every Grid property
change touches the settings store from on_property_changed.

### Physics_material (`src/erhe/physics/erhe_physics/physics_material.cpp`, section 4.12)

| Property | Storage | Notes |
|---|---|---|
| static_friction, dynamic_friction, restitution | entry | inherits; bodies re-snapshot through the Node_physics observer |
| friction_combine, restitution_combine | entry | inherits; enumerations |
| linear_damping, angular_damping | entry | inherits; applied to every live body of the material |
| wind_receptivity | entry | inherits; read by the scene wind each fixed step |
| density | entry | inherits; the mass of a body without an explicit mass |

### Collision_filter (`src/erhe/physics/erhe_physics/collision_filter.cpp`, section 4.21)

| Property | Storage | Notes |
|---|---|---|
| collision_systems, collide_with_systems, not_collide_with_systems | entry | `string[]`, `Array_size::editable`, `native_gltf`; does not inherit; bodies recompile through the Node_physics observer |

### Physics_joint_settings (`src/erhe/physics/erhe_physics/physics_joint_settings.cpp`, section 4.22)

Eleven properties for each of the six degrees of freedom `trans_x`,
`trans_y`, `trans_z`, `rot_x`, `rot_y`, `rot_z` (66 in all), registered by a
table walk; the axis token is the prefix of the name and names the UI group.
All of them are `native_gltf` and inherit, and all of them refresh the two
`Constraint_axis_*` mirrors the constraint is built from.

| Property | Storage | Notes |
|---|---|---|
| &lt;axis&gt;_limit | entry | `Joint_axis_limit` (Free / Limited) |
| &lt;axis&gt;_limit_min, &lt;axis&gt;_limit_max | entry | shown while the axis is Limited, in degrees on a rotation axis; value source `default` is the unbounded side |
| &lt;axis&gt;_limit_stiffness, &lt;axis&gt;_limit_damping | entry | shown while the axis is Limited; zero stiffness is a hard limit |
| &lt;axis&gt;_drive | entry | `Joint_axis_drive` (Off / Force / Acceleration); acceleration is mirrored as force with one warning per item |
| &lt;axis&gt;_drive_max_force | entry | shown while the axis is driven; zero is the unlimited force |
| &lt;axis&gt;_drive_position_target, &lt;axis&gt;_drive_velocity_target | entry | shown while the axis is driven, in degrees on a rotation axis |
| &lt;axis&gt;_drive_stiffness, &lt;axis&gt;_drive_damping | entry | shown while the axis is driven; stiffness greater than zero selects a position motor |

### Rendertarget_mesh (`src/editor/rendertarget_mesh.cpp`, section 4.15)

| Property | Storage | Notes |
|---|---|---|
| width, height, pixels_per_meter | computed | read-only; the size is authored through resize_rendertarget |

### Animation (`src/erhe/scene/erhe_scene/animation.cpp`, section 4.16)

| Property | Storage | Notes |
|---|---|---|
| first_time, last_time, sampler_count, channel_count | computed | read-only over the samplers and channels; notify_keyframes_changed pushes to expressions |

### Joint (`src/editor/scene/joint.cpp`, section 4.17)

| Property | Storage | Notes |
|---|---|---|
| body_0 | entry | weak object reference (D28) to the first frame node; per instance, does not inherit |
| body_1 | entry | weak object reference (D28) to the second frame node; null anchors the joint to the world; does not inherit |
| joint_settings | entry | object reference to a Physics_joint_settings; inherits (D30) |
| enable_collision | entry | inherits; a change rebuilds the constraint |

### Ik, attached (`src/editor/scene/ik_properties.cpp`, section 4.19)

Attached to `erhe::scene::Node`, set on the bone node; none of them inherits,
and each is listed on a node carrying `Item_flags::bone`.

| Property | Storage | Notes |
|---|---|---|
| Ik.lock_x, Ik.lock_y, Ik.lock_z, Ik.limit_x, Ik.limit_y, Ik.limit_z | attached | read into the Ik_settings_data record by read_ik_settings |
| Ik.limit_min, Ik.limit_max | attached | radians shown in degrees; coerced per component to [-pi, 0] / [0, pi] |
| Ik.stiffness | attached | scales the joint's per-iteration change in the constrained IK solve; coerced to [0, 0.99] |
| Ik.rest_rotation | attached | computed default (D31): the bone's bind-pose local rotation, identity without one; "Set rest from current pose" writes the local value |
| Ik.pole_target | attached | weak object reference (D28), Item_type::xformable; any node is accepted and admissibility is decided per drag |
| Ik.pole_angle | attached | radians shown in degrees, not coerced (the angle is periodic) |

### Layout (`src/erhe/scene/erhe_scene/layout.cpp`, section 4.13)

Container values, attached to Node, UI group `Layout`:

| Property | Storage | Notes |
|---|---|---|
| type | attached | the group's KEY property; `none` (the default) means the node is no layout node; does not inherit |
| primary, secondary, tertiary, volume_min, volume_max, gap, grid_track_count | attached | inherits (D30, from the node chain), listed on layout nodes; track count validated to at least 1 per axis, visible for grid |
| grid_track_extent_x, grid_track_extent_y, grid_track_extent_z | attached | float_array, inherits, visible for grid; empty = uniform tracks, a non-empty list held by a layout node is coerced to the axis track count; a "Custom track sizes on / off" row action seeds it from the volume |

Per-child hints, attached (section 4.14), set on the child Node:

| Property | Storage | Notes |
|---|---|---|
| Layout.align_x, Layout.align_y, Layout.align_z | attached | enumeration, listed on children of a layout node |
| Layout.margin_min, Layout.margin_max | attached | |
| Layout.grid_cell_auto, Layout.grid_span | attached | listed under a grid layout; span at least 1 |
| Layout.grid_cell | attached | listed when the cell is not automatic; non-negative |

### Brush_placement (`src/editor/brushes/brush_placement.cpp`, section 4.11)

Attached to `erhe::scene::Node`, group "Brush Placement", none of them
inheriting and none of them serialized (session values, D5).

| Property | Storage | Notes |
|---|---|---|
| Brush_placement.brush | attached | KEY property; object reference (D28), null or a Brush |
| Brush_placement.facet, Brush_placement.corner | attached | developer-only; -1 = NO_INDEX; listed on the nodes carrying the group |

### Brush (`src/editor/brushes/brush.cpp`)

| Property | Storage | Notes |
|---|---|---|
| material | member | object reference, a Material; no clear (a brush keeps a material) |

### Geometry_graph_mesh (`src/editor/geometry_graph/geometry_graph_mesh.cpp`)

Attached to `erhe::scene::Node` (section 4.25), group "Geometry Graph Mesh".

| Property | Storage | Notes |
|---|---|---|
| Geometry_graph_mesh.graph_mesh | attached | KEY property; strong object reference, a Graph_mesh asset; no serialize flag (the native carriers hold the binding); a write reaches the scene's Geometry_graph_mesh_system, which releases the old products and applies the new bake |

### Geometry graph nodes (`src/editor/geometry_graph/nodes/`, section 4.5)

One owner type per node kind; every parameter is `member` with
`mark_node_dirty` as after_set unless listed as a bridge.

| Node | Properties | Storage |
|---|---|---|
| Mesh_box_node | size, subdivisions, power | member |
| Mesh_cone_node | height, radius, use_bottom, slices, stacks | member |
| Mesh_disc_node | outer_radius, inner_radius, slices, stacks | member |
| Mesh_sphere_node | radius, slices, stacks | member |
| Mesh_torus_node | major_radius, minor_radius, major_steps, minor_steps | member |
| Conway_node | kis_height, truncate_ratio, chamfer_ratio, gyro_ratio | member |
| Subdivide_node | mode, iterations | member |
| Boolean_node | operation | member |
| Math_node | operation, a, b | member |
| Transform_node | translation, rotation_mode, rotation, rotation_quaternion, scale | member |
| Distribute_points_node | count, seed | member |
| Instance_on_points_node | scale, align | member |
| Scene_mesh_geometry_node | primitive | member |
| Geometry_output_node | name, physics, physics_motion | member |
| Float_value_node, Integer_value_node, Vector_value_node | value | member |
| Lattice_node | cage_min, cage_max, regenerate_attributes, show_cage | member |
| Lattice_node | auto_fit, divisions, interpolation | bridge (cage freeze, offset resample) |
| Sdf_sphere_node | radius, center | member |
| Sdf_capsule_node | p0, p1, radius0, radius1 | member |
| Sdf_sphere_node, Sdf_capsule_node, Sdf_from_geometry_node | voxel_size | bridge |
| Sdf_to_geometry_node | adaptivity | member |
| Sdf_boolean_node | operation | member |
| Sdf_offset_node | distance | member |
| Sdf_smooth_node | iterations | member |

Not properties: `Asset_reference` pickers, the lattice control points,
and the nodes with no parameters (transform_from_node, passthrough,
join, unary-op, groups, brush source).

### Texture graph nodes (`src/editor/texture_graph/texture_graph_properties.cpp`, section 4.5)

One owner type per descriptor, registered at startup: one `bridge` per
float, color, enumeration, bool and size parameter plus the seed of a
seeded descriptor. Gradient and curve parameters are not properties.

## Not yet migrated

The table is empty: every hand-written row of the Properties window that was
authored state is a registered property.

Rows that are not properties and stay hand-written: read-only
diagnostics (geometry and buffer mesh counts, texture dimensions,
raytrace state, skin joints, rigid body label / position / activity /
shape / inertia, brush polygon counts, the id and the flag word), and list
editors of records (value groups, samplers, animation channels and
samplers). A list of scalars is not one of them:
it is an array property (D34).
