# Property inventory

The status of every editor-visible item field with respect to the
property system (`erhe::property`, design record `doc/property-system.md`):
which fields are registered properties, how each is stored, and which
fields the Properties window still draws by hand. This document is the
owner of that status; the design record's sections 4.1 to 4.14 own the
design of each migration and refer here for the per-field list.

Update this document in the same commit as any registration added,
removed or changed in storage kind, and whenever a hand-written row is
migrated or added. The same commit updates the owner's subsection in
`doc/property-system.md` (4.1 to 4.14) when the design changed, and
`src/erhe/property/notes.md` when a library mechanism changed; the design
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

The Properties window tints a row's label by its value source (D12):
member and bridge rows are blue, entry rows green / gray / cyan / orange /
purple by layer, computed rows dim gray. Untinted rows are hand-written.

## Registered properties

### Item_base (`src/erhe/item/erhe_item/item.cpp`)

| Property | Storage | Notes |
|---|---|---|
| visible | entry | flag mirror |
| purpose | entry | USD purpose enumeration, `inherits`; its default layer is per-object (D31), derived from the editor-only flag bits (`src/erhe/item/notes.md` "Purpose") |
| style | bridge | object reference to the item's style source (doc/style-library.md D3), style items only |
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

### Camera (`src/erhe/scene/erhe_scene/camera.cpp`, section 4.4)

| Property | Storage | Notes |
|---|---|---|
| projection_type, infinite_z_far | entry | inherits (D30); mirrored into `Projection` by `on_property_changed` |
| fov_x, fov_y, fov_left, fov_right, fov_up, fov_down | entry | inherits; mirrored; angle rows |
| ortho_left, ortho_width, ortho_bottom, ortho_height, frustum_left, frustum_right, frustum_bottom, frustum_top, z_near, z_far | entry | inherits; mirrored; logarithmic extents |
| exposure, shadow_range | entry | inherits |

### Node_physics (`src/editor/scene/node_physics.cpp`, section 4.10)

| Property | Storage | Notes |
|---|---|---|
| motion_mode | entry | inherits (D30); enumeration, mirrored into the intended mode, sets the body's effective mode |
| is_trigger | entry | inherits; mirrored into the create info, recreates the body |
| gravity_factor | entry | inherits; mirrored, pushed to the live body |
| initial_linear_velocity, initial_angular_velocity | entry | inherits; mirrored, no live consequence |
| mass | entry | inherits; source default = shape mass scaled by the material density, else scales the body's inertia |
| center_of_mass_offset | entry | inherits; realized as the collision shape wrapper, recreates the body |
| physics_material, collision_filter | entry | inherits; object references, a holder assigns them to every body below |

### Grid (`src/editor/grid/grid.cpp`, section 4.11)

| Property | Storage | Notes |
|---|---|---|
| plane_type, center, rotation | entry | inherits (D30, from the node chain); on_property_changed re-derives the transform |
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

### Rendertarget_mesh (`src/editor/rendertarget_mesh.cpp`, section 4.15)

| Property | Storage | Notes |
|---|---|---|
| width, height, pixels_per_meter | computed | read-only; the size is authored through resize_rendertarget |

### Animation (`src/erhe/scene/erhe_scene/animation.cpp`, section 4.16)

| Property | Storage | Notes |
|---|---|---|
| first_time, last_time, sampler_count, channel_count | computed | read-only over the samplers and channels; notify_keyframes_changed pushes to expressions |

### Node_joint (`src/editor/scene/node_joint.cpp`, section 4.17)

| Property | Storage | Notes |
|---|---|---|
| connected_node | bridge | node-typed object reference over the weak member (no node-to-node cycle); local only; refuses the joint's own node |
| joint_settings | entry | object reference to a Physics_joint_settings; inherits (D30) |
| enable_collision | entry | inherits; the mirror follows and the constraint rebuilds |

### Layout (`src/erhe/scene/erhe_scene/layout.cpp`, section 4.13)

| Property | Storage | Notes |
|---|---|---|
| type, primary, secondary, tertiary, volume_min, volume_max, gap, grid_track_count | entry | inherits (D30, from the node chain); the members update() reads mirror the effective values; track count validated to at least 1 per axis, visible for grid |

Not properties: the per-axis grid track extent lists.

Per-child hints, attached (section 4.14), set on the child Node:

| Property | Storage | Notes |
|---|---|---|
| Layout.align_x, Layout.align_y, Layout.align_z | attached | enumeration, listed on children of a layout node |
| Layout.margin_min, Layout.margin_max | attached | |
| Layout.grid_cell_auto, Layout.grid_span | attached | listed under a grid layout; span at least 1 |
| Layout.grid_cell | attached | listed when the cell is not automatic; non-negative |

### Brush_placement (`src/editor/brushes/brush_placement.cpp`, section 4.11)

| Property | Storage | Notes |
|---|---|---|
| brush | entry | object reference (D28), null or a Brush; inherits (D30); the mirror pointer follows |
| facet, corner | entry | developer-only; -1 = NO_INDEX; inherits; set_corner writes the store |

### Brush (`src/editor/brushes/brush.cpp`)

| Property | Storage | Notes |
|---|---|---|
| material | member | object reference, a Material; no clear (a brush keeps a material) |

### Geometry_graph_mesh (`src/editor/geometry_graph/geometry_graph_mesh.cpp`)

| Property | Storage | Notes |
|---|---|---|
| graph_mesh | member | object reference, a Graph_mesh asset; after_set releases the controlled products and applies the new bake |

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

Hand-written rows of the Properties window that are authored state, in
migration priority order. Each migration follows the Material recipe
(design record section 4.1).

| Owner | Fields | Notes |
|---|---|---|
| Physics_joint_settings | limits, drives | lists; no `Property_value` form, stay hand-written |
| Collision_filter | collision_systems, collide_with_systems, not_collide_with_systems | string lists; stay hand-written |
| Layout | grid track extents | per-axis float lists; stay hand-written |
| Scene | ambient light | the per-scene overrides stay a settings block |

Rows that are not properties and stay hand-written: read-only
diagnostics (geometry and buffer mesh counts, texture dimensions,
raytrace state, skin joints, rigid body label / position / activity /
shape / inertia, brush polygon counts, the id and the flag word), and list
editors (attachments, samplers, animation channels and samplers, joint
limits and drives).
