# erhe scene and data model audit

Read-only architectural and API audit, 2026-09-30. Scope: src/erhe/item,
src/erhe/scene, src/erhe/property, src/erhe/primitive, src/erhe/geometry,
src/erhe/gltf, src/erhe/usd, src/erhe/physics, src/erhe/math,
src/erhe/message_bus, src/erhe/commands, src/erhe/graph and their documents
under doc/erhe/ and doc/gltf_extensions/ (the "Scene and data model" row of
doc/reference/audit_erhe_2026_09_30.md). Every claim
below was verified by reading the referenced lines at HEAD 7445e65e6;
nothing was built or run. Paths under a library are relative to its
`erhe_<name>/` source directory unless spelled in full.

Slice size (non-test .cpp/.hpp): item 3.1k, scene 12.0k, property 5.6k,
primitive 10.1k, geometry 18.6k (1.9k vendored mikktspace), gltf 9.3k,
usd 20.5k, physics 13.4k, math 4.4k, message_bus 0.1k, commands 4.2k,
graph 0.8k lines; 102k in all.

--------------------------------------------------------------------------------
## 1. Architecture

### 1.1 The object model is a USD prim hierarchy, not Node plus attachments

- The brief's "Node/attachment" model is gone: `Node_attachment` was deleted
  on 2026-09-22 (memory-bank/topics/property_system.md, P11b). The class
  chain mirrors USD schema levels: `Item_base` -> `Hierarchy` -> `Typed` ->
  `Imageable` -> `Xformable` (alias `Node`) -> `Xform` | `Boundable` ->
  `Gprim` -> `Mesh` | `Point_instancer`; `Camera` and `Light` derive from
  `Xformable`; `Scope`, `Skin`, `Animation`, `Material` from `Typed`
  (src/erhe/item/erhe_item/typed.hpp:29, src/erhe/scene/erhe_scene/imageable.hpp:18,
  node.hpp:82-89, gprim.hpp:23-30, mesh.hpp:95, camera.hpp:25, skin.hpp:33,
  src/erhe/primitive/erhe_primitive/material.hpp:3-4). `using Node = Xformable`
  is declared retired-when-complete (node.hpp:291-293). A light or camera is
  a child prim with its own transform, "the mesh of a node" is `get_mesh()`
  (mesh.hpp:268-272), any prim may parent any prim, and `Scope` has no
  transform so propagation recurses through it (scene.cpp:386-397).
- Each level is a CRTP `Item<Base, Intermediate, Self, Item_kind>` supplying
  `clone()`, a bitmask `get_type()` and the property owner-type id
  (item.hpp:494-522); type tests are bit-subset tests (797-810). `Scene` is
  thin (scene.hpp:298-334) and membership follows tree structure through
  `Xformable::handle_item_host_update` (node.cpp:387-419).

Editor concepts hard-wired into the library:

- `Item_flags` has 52 bits (item.hpp:35-278); at least 22 are editor or
  session state: `tool`/`brush`/`controller`/`rendertarget` (54-57),
  `expand` (58), the two hover bits (59-60), `show_in_developer_ui` (63),
  `bone_proxy` (94), `render_proxy`/`proxy_hidden` (100-111), the four
  graph/descendant hover bits (117-128), `active_item` (165),
  `view_anchored` (187). Comments name the owners ("maintained by
  Hover_tool", "Written only by editor::Selection", "the editor hotbar quad").
- `Item_type` has 51 bit indices whose label table "must match the C++ class
  names" (item.hpp:280-445, 391); about 24 name classes that exist only
  under src/editor (`brush`, `composer`, `grid`, five `asset_file_*`,
  `content_library_*`, `graph*`, `style`, ...): an editor class edits the library.
- `Item_host::hosted_selection` is a public vector, "Always access through
  Selection::get_hosted_selection()" (item_host.hpp:49-56), and `Purpose`
  derives `guide` from the editor-only flag bits (item.hpp:213-219, 654-660).

Verdict: a coherent USD-shaped model with clean levels whose flag and type
tables have become the editor's; `erhe::item` is not reusable outside this
editor without editing it.

### 1.2 Ownership, cycles and the item_host_mutex discipline

- Children are `vector<shared_ptr<Hierarchy>>`, the parent a `weak_ptr`
  (hierarchy.hpp:197-199); `Item_base` is `enable_shared_from_this`
  (item.hpp:525). Cross references are strong where they are data
  (`Mesh_primitive` -> `Primitive`, `Material`, mesh.hpp:74-75;
  `Skin_data::joints`, skin.hpp:28-30; `Object_reference`,
  property_value.hpp:31-35) and weak for node-to-node links
  (`Weak_object_reference`, 41-48); style and reference sources refuse
  cycles (dependency_object.hpp:210, 230).
- Raw back-pointers for hosts (node.hpp:60, mesh.hpp:253, item.hpp:776,
  scene.hpp:322-328) rely on ordered teardown: `Scene::sever_host()` exists
  because "the Scene and/or its content may be co-owned elsewhere
  (selection, undo stack, clipboard)" (scene.hpp:91-100, scene.cpp:265-279).
- `item_host_mutex` is a plain `std::mutex` per host with a static orphan
  fallback (item_host.hpp:46-47, 63-70). The library locks it in one place,
  `Scene::update_node_transforms` (scene.cpp:306-316); the discipline
  "callers hold the Item_host mutex" is comment-only (scene.hpp:150-152),
  with 11 references in src/erhe against 51 in src/editor (grep): applied at
  call sites, not enforced, as the editor report says.
- The transform serial (node.cpp:211-220) and the item mutation serial
  (item.hpp:461-468) are non-atomic statics by design; `Unique_id` is atomic.

Verdict: ownership is sound (weak up, shared down, strong across only for
data); the host mutex is a convention with one library-side lock.

### 1.3 The property system

Problem solved: one store for authored item state so the Properties window,
undo, MCP, styles, prefab references, animation and both file formats
address every field by name under one precedence rule; a WPF port
(dependency_object.hpp:63-67, doc/reference/property_system_wpf_comparison.md).

- Precedence coerced > animated > local > style > reference > inherited >
  default is one walk in `get_base_value` (dependency_object.cpp:574-611,
  ancestor walk 520-529); entries are a sorted vector searched by
  `lower_bound` (181-191), and an unauthored value has no entry.
- Registration is static-initialization time (dependency_property.hpp:120-124);
  owner types form a per-class tree (item.hpp:514-521) so metadata overrides
  resolve by chain (dependency_property.hpp:62-68). The value is a
  20-alternative `std::variant` whose array and string members allocate on
  copy, "not on the per-frame hot path" by convention (property_value.hpp:60-89).
- Per-node cost: `Dependency_object` adds a vector, two `shared_ptr`, three
  `unique_ptr`, a pending vector, an int and a bool (dependency_object.hpp:523-532);
  `Item_base` adds name, tag set, uid, path pointer, label, flags, id and two
  host pointers (item.hpp:776-794); `Xformable` adds two `Trs_transform`
  (two `mat4` plus deferred TRS each, trs_transform.hpp:60-64) and two lazy
  side objects (node.hpp:282-283). By member inspection an empty `Xform` is
  on the order of 800 bytes; not measured with `sizeof`.
- Read cost: typed `get_value` returns a `Property_value` by value through
  the layer walk on every call (dependency_object.hpp:119-123). Every Light
  property is `inherits` and read directly by the getters (light.hpp:280-314);
  `is_active()` reads five per call (228-241) and the light buffer three per
  light per pass (src/erhe/scene_renderer/erhe_scene_renderer/light_buffer.cpp:733),
  so an unauthored light walks its ancestors per read: no allocation, but a
  per-frame variant walk where a mirror would be a load.
- Members versus properties, four arrangements coexist: (1) bridged single
  storage: node TRS writes the `Trs_transform` in place (node.hpp:224-231),
  flag bits bridge `set_flag_bits` (item.hpp:689-700); (2) entry store plus
  a mirror refreshed in `on_property_changed`: `Camera::m_projection`
  (camera.hpp:50-53, 117-121), `Scene::m_ambient_light` (scene.hpp:279-298),
  `Material_data` ("Every slot field is a mirror", material.hpp:96-100);
  (3) entry store read per frame: `Light`; (4) derived flag bits mirroring
  property effective values, which `set_flag_bits` drops with a log line
  (item.hpp:204-211, item.cpp:547-556).
- Dual state that can drift: `Mesh_primitive::primitive` and `material` are
  public members while `material_property` is `register_member` over
  `material` and `set_primitive_material` is documented as "the one writer"
  (mesh.hpp:57-59, 74-75, 172-177); a member write bypasses the host notify.
- Serialization: import writes every field then
  `clear_default_valued_local_properties` restores the authored/unauthored
  distinction (dependency_object.hpp:535-548, gltf_fastgltf.cpp:4163);
  `Property_flags::native_gltf` marks native carriers
  (property_metadata.hpp:82-90); everything else rides
  `ERHE_node.properties` as name -> text (gltf_item_flags.hpp:50-55,
  property_string.cpp:135, :255). Change consequences are data flags the
  editor interprets (property_metadata.hpp:74-80).
- Propagation: `set_value` -> validate/coerce -> `notify` -> callback, hook,
  observers -> descendants -> style and reference users
  (dependency_object.hpp:462-469, 489-512), coalesced by `Change_batch`
  (342-351); tree moves snapshot and re-apply (hierarchy.cpp:204-244).

Verdict: the most complete subsystem in the slice and the source of the
editor's generic rows, undo and MCP surface; its costs are header weight
(2.4), the per-read walk on unmirrored classes, and four member/property
arrangements a reader must know.

### 1.4 Threading and geogram rules

- Stated contracts: property values are "guarded by the item's host mutex"
  (doc/erhe/property.md:339-345); `Scene_host` mesh callbacks "may be called
  from worker threads ... must only enqueue" (scene_host.hpp:38-41); lazy
  inverse and decompose are "not thread-safe against concurrent first reads"
  (transform.hpp:48-53, trs_transform.hpp:48-51); `Primitive_shape` states
  the lock order Item_host -> build -> state (primitive.hpp:82-97).
- geogram: one process-global recursive `geogram_lock()` with its rationale
  in the header (geometry.hpp:47-66), 33 lock sites (grep), the
  `-ffp-contract=off` build contract (doc/erhe/geogram.md), reproducer dumps
  on failure (geometry.cpp:742-806, remesh.cpp:62-143). Gap: no library
  check that a hosted-item writer holds the host mutex.

### 1.5 glTF as the native format

- One 7.5k-line translation unit: `Gltf_parser` (gltf_fastgltf.cpp:1157-3504),
  `parse_gltf` 632 lines (3798), `Gltf_exporter` (4668-7492), `export_gltf`
  346 (7145). `parse_gltf` is device-free by contract so it runs on workers;
  GPU residency is a separate object (gltf_fastgltf.hpp:156-214).
- Trace: a `Node` with a `Mesh` child exports through `process_node` (6474)
  as TRS plus `ERHE_node` (flags by name, `properties`, `prim_class`),
  `ERHE_geometry` and `ERHE_material` (gltf_item_flags.hpp:50-77); import
  `parse_node` (3109) creates the prim by class, `parse_node_transform`
  (2029) keeps TRS, `apply_persistent_flags_and_properties` (574) replays
  the local set after the default elision (4163).
- What round-trips is tabulated in doc/erhe/scene_format_support.md:18-93
  and matches the code. Not carried: sparse accessors, morph targets,
  non-indexed primitives, custom attributes, most `KHR_materials_*`,
  `KHR_animation_pointer`, text `.gltf` re-import.
- Extension proliferation: eleven `ERHE_*` names in the importer (grep:
  ERHE_geometry 25 sites, ERHE_node 21, ERHE_material 12, ERHE_camera 11,
  ERHE_light 9, ERHE_asset_reference 7, and four more), each documented
  under doc/gltf_extensions/, prefix unregistered (scene_format_support.md:85-92);
  legacy `extras` still parsed (gltf_fastgltf.cpp:81). The library/editor
  split (five typed extensions in the library, raw JSON payloads injected
  through `Gltf_export_extension_payloads`, gltf_fastgltf.hpp:431-445) is the
  right seam; the cost is twelve documents to answer "what is in this file".
- glTF 2.1 `files`/`externalAssets`/`uid` ride a fastgltf fork
  (gltf_fastgltf.hpp:64-90); uids are item identity (item.hpp:786-793).

Verdict: glTF is the native format in fact, carries every authored property
by name, and is verified end to end only by scripts/scene_roundtrip_verify.py
through the editor; no unit test covers it (section 5).

### 1.6 USD

`erhe::usd` is the largest library in the slice (usd_import.cpp 8308,
usd_export.cpp 7662) over the LightUSD fork (src/erhe/usd/CMakeLists.txt:27-39),
linking `erhe::scene` PRIVATE: an importer/exporter, not a second model. The
object model was reshaped for it: `Typed` keeps unknown prims (typed.hpp:17-23),
xformOp stacks survive next to the composed TRS (node.hpp:155-174),
composition arcs sit on `Typed` (61-76), `Instance_override` carries sparse
overrides (instance_override.hpp:40-80), `Point_instancer` expands to child
prims (point_instancer.hpp:13-22). Coverage: scene_format_support.md:94-177;
USD builds only on Windows wrappers and Android (102). 405 tests.

### 1.7 Physics abstraction

- Interfaces `IWorld` (26 virtuals), `IRigid_body` (42), `ICollision_shape`
  (18), `IConstraint`, plus neutral materials, filters, joint settings,
  `Joint_reach` and a six-DOF classifier; three backends (jolt 22 files,
  box3d 24, null). Neutral `Transform` is `mat3 basis + vec3 origin` "to
  match Jolt" (physics/transform.hpp:7-25, doc/erhe/physics.md).
- The infra report's edge is confirmed and half-fixed: `IWorld::debug_draw`
  takes `erhe::renderer::Jolt_debug_renderer&` (iworld.hpp:11-13, 57) and
  physics links `erhe::renderer` (CMakeLists.txt:126) while a renderer-free
  `IDebug_draw` exists unused (idebug_draw.hpp:7-49). `IWorld::create()`
  returns a raw owning pointer beside `create_shared` / `create_unique`
  (33-35). `erhe::scene::Physics_description` is the format-neutral carrier
  both readers fill, "data-only" by header contract (physics_description.hpp:1-12).

Verdict: a real abstraction with backend-neutral tests, plus one debug-draw
type dragging a level-8 dependency into a level-7 library.

### 1.8 Math versus glm

`erhe::math` is glm plus helpers: `Aabb`, `Sphere`, `Viewport`, `Input_axis`,
Euler conversions, and a 1248-line `math_util.hpp` (108 top-level
declarations) whose `unproject` / `project_to_screen_space` redo
`glm::unProject` / `glm::project` with a configurable depth range
(math_util.hpp:277, 320; doc/erhe/math.md:29). Five rigid-transform
representations cross the scene API: `glm::mat4`, `erhe::scene::Transform`,
`Trs_transform`, `Xform_op_stack`, `erhe::physics::Transform`; `Xformable`
takes three of them in twelve setter overloads (node.hpp:213-222).

### 1.9 Message bus and the four notification mechanisms

- `erhe::message_bus` is 135 lines: `Message_bus<T, policy>` with sync,
  queued or both dispatch enforced by `requires` (message_bus.hpp:46-109),
  weak subscriptions, three mutexes (124-131), a "TODO use signals" (45).
  All 24 buses are in src/editor/app_message_bus.hpp; no `src/erhe` library
  publishes or subscribes, and `erhe::item` links it without an include
  (src/erhe/item/CMakeLists.txt:36): a dead edge.
- The scene layer notifies through three other mechanisms: virtual
  `Scene_host` callbacks (scene_host.hpp:55-70), `INode_system`
  (node_system.hpp:25-45, scene.hpp:243-258), property observers and
  `Transform_observer` (dependency_object.hpp:323-326, node.hpp:202-208).
  Four designs is two too many.

### 1.10 Commands and input bindings

`erhe::commands` (4.2k lines) is a registry with eleven binding kinds
(commands.hpp:255-265), user overrides with conflict detection (54-70,
151-173) and a recursive mutex because commands re-enter (80-84, 248). The
public header includes the three XR binding headers unconditionally (9-13)
and links `erhe::xr` PUBLIC (CMakeLists.txt:48), the infra report's edge 6;
only `dispatch_xr_events` is guarded (204-206).

### 1.11 Confirming the other reports

- Rendering 1.3/2.7: renderers hold `shared_ptr<Mesh>` in draw-list entries
  (draw_list_scene.hpp:165-198, 258) and `Light_set` holds
  `shared_ptr<Light>` (light_set.hpp:63); the scene hands them a
  `weak_from_this().lock()` per moved mesh per frame (mesh.cpp:602-609).
- Infra 1: the `erhe_log` -> geogram, `erhe_physics` -> `erhe::renderer`
  (1.7) and `erhe_commands` -> `erhe::xr` (1.10) edges are confirmed; add
  `erhe_item` -> `erhe::message_bus` unused (1.9) and `erhe_gltf` PUBLIC
  `erhe::scene` + `erhe::math` (src/erhe/gltf/CMakeLists.txt:36-41).
- Editor 1: `item_host_mutex` in MCP handlers confirmed
  (src/editor/mcp/mcp_server_scene_action.cpp, 4 sites); `Scene_root` is the
  only `Scene_host` (637 + 2733 lines); src/editor/scene/scene_view.hpp:12
  includes geogram directly and would get it through mesh.hpp anyway (2.4).

--------------------------------------------------------------------------------
## 2. API issues

### 2.1 Naming

- `Node` / `Xformable`: the `class Xformable; using Node = Xformable;` line
  is repeated in ten headers (scene.hpp:26, scene_host.hpp:14, skin.hpp:15,
  irigid_body.hpp:14, gltf_fastgltf.hpp:47, usd.hpp:36, ...).
- `set_parent` preserves the WORLD transform, `set_prim_parent` /
  `set_mesh_parent` the LOCAL one (node.hpp:295-300, mesh.hpp:274-276,
  node.cpp:298-314): one verb, opposite semantics, told apart by a prefix.
- `Item_flags::id` / `content` / `tool` are pass-membership bits named as
  nouns; `no_message` / `no_transform_update` are negatives (item.hpp:39-40, 52-54).
- `Motion_mode` values carry `e_` (irigid_body.hpp:20-26); `Animation_path`
  enumerators are UPPER_CASE (animation.hpp:20-33).
### 2.2 Boolean arguments

50 header declarations take a `bool` (grep; physics 11, math 11, item 7,
scene 5, primitive 5, geometry 5, property 2, graph 2, commands 2):
`set_flag_bits(mask, bool)` and four setters (item.hpp:712-758),
`hierarchy_sanity_check(bool = false)` (hierarchy.hpp:123, node.hpp:210),
`update_subtree_transforms(prim, bool carry_body_driven)` (scene.hpp:294),
`bind_command_to_mouse_drag(..., bool call_on_button_down_without_motion, ...)`
(commands.hpp:138-143), `set_collision_enabled(a, b, bool)` (iworld.hpp:71),
`export_gltf(root, bool binary, physics)` (gltf_fastgltf.hpp:557-561).
Setters over boolean properties are defensible; the mode flags violate the rule.

### 2.3 Mutable getters and public state

`get_mutable_children()` beside `get_children()` (hierarchy.hpp:107-108,
5 users); four `Scene` getters with mutable overloads (scene.hpp:186-189,
214-217); `Mesh_layer` / `Light_layer` fields public (52-55, 69-71);
`Mesh::layer_id`, `skin`, `point_size`, `line_width` (mesh.hpp:215-218);
`Xformable::node_data` with `mutable` serials and dirty bits inside
(node.hpp:24-44, 267); `Skin::skin_data` and the animation containers
(skin.hpp:56, animation.hpp:56-58, 71-80); `Graph::m_nodes` / `m_links`
public despite the prefix (graph.hpp:47-49); `hosted_selection` (1.1).

### 2.4 Header weight

Transitive include closure over src/erhe (scratch script; erhe headers,
plus the third-party roots they name):

| Header | erhe headers | lines | third-party pulled |
|---|---|---|---|
| erhe_item/item.hpp | 10 | 2854 | glm |
| erhe_scene/node.hpp | 19 | 3946 | glm |
| erhe_scene/scene.hpp | 15 | 3582 | glm |
| erhe_scene/mesh.hpp | 37 | 6944 | geogram mesh.h, glm |
| erhe_primitive/primitive.hpp | 16 | 2622 | geogram mesh.h |
| erhe_commands/commands.hpp | 15 | 1384 | volk, Tracy, nvtx (via erhe_profile) |

Naming a `Node` compiles the whole property system plus `<filesystem>` and
`<set>` (item.hpp:7-11); naming a `Mesh` adds geogram (primitive/build_info.hpp:5)
and `erhe_graphics/texture.hpp` (material.hpp:10); `item_host.hpp:3` pulls
the profiler header for its mutex macro.

### 2.5 Hidden ordering

- "This must come *after* node_data.host has been updated" (node.cpp:407-411);
  `Scene::sever_host` before the host dies, children before root
  (scene.hpp:91-100, scene.cpp:270-278); node systems added "before the
  scene holds nodes the system cares about", unchecked (scene.hpp:243-248).
- Property registration in static initializers (dependency_property.hpp:120-124,
  item.hpp:626-688); `Hierarchy::set_parent` needs a `shared_ptr`-managed
  object (hierarchy.cpp:224-229); `Xformable::set_parent` "does not care
  about transforms of orphan nodes" (node.cpp:307-313).

### 2.6 God classes

`Item_base` (item.hpp:524-795: identity, flags, name, tags, path, uid,
purpose, style, seal, 15 static properties, active derivation, pruning);
`Dependency_object` (550-line header, 1854-line source); `Xformable`
(twelve transform setters, xformOp stack, animated layer, observers, nine
lock properties, node.hpp:82-289); `Gltf_parser` 2.3k and `Gltf_exporter`
2.8k lines in one .cpp; `Geometry` (1069-line header, geometry.hpp:276-318).

### 2.7 Duplicated concepts

- Transform: five representations (1.8). Identity: `Unique_id<Item_base>`,
  `m_gltf_uid`, `m_name`, `get_path()`, `Debug_label`, `make_graph_id()`
  (graph.hpp:18-22), `Layer_id`; and three type identities per object:
  `Item_type` bit, property `Owner_type`, the `typeName` token
  (item.hpp:509-521, typed.hpp:44-59).
- Flags: `Item_flags` (52), `Item_type` (51), `Item_filter`
  (item.hpp:447-459), `Property_flags` (property_metadata.hpp:74-96),
  `Mesh_layer::flags`, the eleven-bool `Shadow_frustum_fit_settings`
  (light.hpp:26-56), `IDebug_draw::c_*` ints (idebug_draw.hpp:20-36).
  `Physics_description` enums duplicate `erhe::physics` ones by design
  (physics_description.hpp:26-49 vs irigid_body.hpp:20-26).

### 2.8 APIs easy to misuse

- 17 copy constructors/assignments that `ERHE_FATAL("TODO")` at run time
  (node.cpp:232-233, camera.cpp:154, light.cpp:213-214, gprim.cpp:60-61,
  boundable.cpp:10-11, xform.cpp:11-12, point_instancer.cpp:11-12) and
  `Scene` copy that `ERHE_FATAL("This probably won't work")`
  (scene.cpp:410-418); `Mesh` deletes them correctly (mesh.hpp:101-102).
- `Mesh_primitive` public members beside "the one writer" (1.3);
  `set_flag_bits` drops derived bits with only a log line (item.cpp:547-556);
  `Item_filter{}` matches everything (item.hpp:455-458); `IWorld::create()`
  raw pointer (1.7).
- `Hierarchy::for_each<T>` runs `dynamic_cast` per visited node
  (hierarchy.hpp:141, 170; 12 editor call sites); `get_path()` "never call
  it per frame" (hierarchy.hpp:67) has 28 callers; `Node_data::diff_mask`
  survives with one commented-out user (node.hpp:64,
  src/editor/operations/node_transform_operation.cpp:28).

--------------------------------------------------------------------------------
## 3. Code health

### 3.1 Largest files and functions (non-test)

Files: usd_import.cpp 8308, usd_export.cpp 7662, gltf_fastgltf.cpp 7510,
geometry.cpp 2128, primitive_builder.cpp 2010, mikktspace.cpp 1900 (vendored),
dependency_object.cpp 1854, math_util.cpp 1797, primitive.cpp 1736,
usd_import_physics.cpp 1555, usd.hpp 1351, math_util.hpp 1248,
commands.cpp 1244, geometry.hpp 1069, node.cpp 1024, jolt_world.cpp 1003.

Functions over 300 lines: `parse_gltf` 632 (gltf_fastgltf.cpp:3798),
`Catmull_clark_subdivision::build` 580
(operation/subdivision/catmull_clark_subdivision.cpp:38),
`Light::tight_directional_light_projection_transforms` 440
(light_frustum_fit.cpp:325), `Chamfer::build` 366
(operation/conway/chamfer_old.cpp:317), `Build_context::take_optimizable_snapshot`
359 (primitive_builder.cpp:1037), `build_buffer_mesh_from_triangle_soup` 348
(primitive.cpp:1097), `Gltf_exporter::export_gltf` 346 (:7145),
`Chamfer3::build` 340 (chamfer3.cpp:40).

### 3.2 Duplication

`chamfer_old.cpp` (752 lines) and `chamfer3.cpp` (390) are two chamfers;
three physics backends implement the same 87 virtuals; two property-to-file
serializers share one text form (usd_export.cpp:628); each `Xformable`
setter exists per transform type (node.hpp:213-222, node.cpp:851-984).

### 3.3 Dead code and stray files

- 17 commented-out code lines in node.cpp (469-486, 495-500), 4 in light.cpp;
  `imotion_state.hpp` is empty; `Node_data::diff_mask` (2.8);
  `Item_flags::render_wireframe` / `render_bounding_volume` are `// TODO`
  (item.hpp:50-51); `erhe_item` -> `erhe::message_bus` link (1.9).
- 41 tracked `src/erhe/*/claude_review.md` and `Readme.md` files sit beside
  the sources against the doc/README.md layout rule;
  src/erhe/scene/claude_review.md (2026-03-22) still describes
  `Node_attachment`, src/erhe/scene/Readme.md dates from 2021-05-01.

### 3.4 TODO / FIXME

100 in non-test sources (grep TODO|FIXME|XXX|HACK): primitive 23, scene 16,
gltf 13, geometry 13, commands 13, math 12, physics 6, item 2, usd 1,
message_bus 1; scene's are mostly the fatal copy constructors (2.8).

### 3.5 Style-rule violations (AGENTS.md yardstick, non-test, mikktspace excluded)

- `struct`: 56. property 12 (property_value.hpp:21-48;
  dependency_object.hpp:53, 378, 406, 413; dependency_property.hpp:235-248),
  geometry 12 (8 in operation/octree.hpp), math_util.hpp:74-100, commands 5,
  gltf_fastgltf.hpp:364 `Gltf_parse_arguments`, message_bus.hpp:30,
  idebug_draw.hpp:10, and singles in six .cpp/.hpp files.
- `auto` locals with initializer: 421 (gltf 92, geometry 89, physics 62,
  scene 53, math 42, primitive 31, item 13, property 12, commands 12, usd 9,
  graph 6); trailing-return `auto f() -> T` is the house style, not counted.
  Boolean parameters: 50 (2.2).
- Logging: no `printf`/`std::cout` logging; the `std::fprintf` at
  geometry.cpp:760-806 and remesh.cpp:83-143 write reproducer sources to a
  `FILE*`. `ERHE_VERIFY` 343 / `ERHE_FATAL` 70 in the slice (primitive
  122/43, geometry 72/1, scene 33/17, gltf 32/3, commands 30/3); usd uses neither.

### 3.6 Allocation in hot paths

- `Scene::update_node_transforms` allocates nothing in steady state: dirty,
  processing and visited containers are members kept for capacity
  (scene.hpp:322-330), the sort is in place (scene.cpp:330-336), the walk is
  a type-bit test plus `static_cast` (386-398), an empty pass costs one
  clock read (318-320). The reference implementation of the AGENTS.md rule.
- `Xformable::handle_transform_update` is six null-checked
  `invalidate_dependents` calls and one dirty mark (node.cpp:445-462).
- `Mesh::update_transform_dependent_state` locks `weak_from_this()` and
  commits every raytrace instance per moved mesh (mesh.cpp:594-609): refcount
  and BVH refit traffic, no heap. `Mesh::get_aabb_world()` recomputes per call
  by design (658-690) while the shadow renderer walks every mesh per light
  (rendering report 6.1): one cached AABB would serve both.
- `light_frustum_fit.cpp` declares 11 function-local vectors (grep) in
  cache-build and debug paths; the per-light fit takes `Shadow_fit_scratch`
  so "steady-state fits perform no heap allocations" (light.hpp:117-121).
- `for_each<T>` `dynamic_cast` and `get_path()` (2.8); `Light` reads per
  frame (1.3); allocating `Property_value` alternatives are kept off the
  frame by convention only (property_value.hpp:77-82).

### 3.7 Doc / code mismatches found

- doc/erhe/item.md:12: `get_property_owner_type()` "returns `get_type()`";
  it returns the registry `Owner_type` allocated per class
  (item.hpp:514-521, 546-547), a different id space.
- doc/erhe/scene.md:14: the `Projection` fields "are registered as bridged"
  properties; camera.hpp:88-91 says "all in the entry store and all
  inherits" and `projection()` is a mirror (camera.hpp:50-53, 117-121).
- doc/erhe/gltf.md:33 lists `erhe::scene` as private; the target makes
  `erhe::scene` and `erhe::math` PUBLIC (src/erhe/gltf/CMakeLists.txt:36-41).
- doc/erhe/graph.md:25 lists `erhe::item` as private while graph.hpp:3-4
  includes item headers publicly and `Graph` derives from `Item_host` (24).
- doc/erhe/scene.md "Key Types" presents `Mesh_primitive::material` as
  property-only while the member is public (1.3); the brief's
  "Node/attachment" model and src/erhe/scene/claude_review.md describe a
  model removed on 2026-09-22.

--------------------------------------------------------------------------------
## 4. Strengths (specific)

- One funnel for every transform write and one change-driven,
  allocation-free, instrumented propagation pass with owner-writes semantics
  for physics-driven subtrees (node.cpp:423-463, scene.cpp:281-408,
  scene.hpp:146-178).
- A property system complete enough to carry the editor: layered
  precedence, sealing, animated layer, expressions, live style and reference
  sources, inheritance snapshots across tree moves, per-object defaults,
  computed and attached properties, each keyed to a numbered decision and
  compared feature by feature with WPF; import restores the
  authored/unauthored distinction (dependency_object.hpp:535-548).
- The USD-shaped prim hierarchy serves two formats from one model; unknown
  prims survive as `Typed` (typed.hpp:17-23); authored xformOp stacks survive
  edits with an explicit collapse rule (node.cpp:728-798).
- Contracts stated where they apply: worker-thread rule on `Scene_host`
  (scene_host.hpp:38-41), lock order on `Primitive_shape` (primitive.hpp:82-97),
  the geogram rule with its upstream reference (geometry.hpp:47-66), why the
  dirty list is raw pointers (scene.hpp:322-327).
- Sibling-unique names with the rule spelled once (hierarchy.hpp:60-92);
  skin use-counting with a change result (scene.hpp:232-239);
  degenerate-input guards before geogram and reproducers after
  (geometry.cpp:742-806, doc/erhe/geogram.md).
- Physics: three backends, backend-neutral tests stepping a real world, a
  written table of every unmapped feature (doc/erhe/physics.md,
  doc/erhe/box3d_physics.md). Commands: user-editable bindings with
  persistence and conflict reporting, rebuilt on change (commands.hpp:72-84).
  doc/erhe/scene_format_support.md is a per-feature import/export matrix for
  both formats that matches the code.

--------------------------------------------------------------------------------
## 5. Test coverage per library

| Library | Files / lines | Cases | Covered | Gaps |
|---|---|---|---|---|
| item | 22 / 3670 | 195 | hierarchy, paths, unique names, flags, filter, visibility, active, purpose, sealing, CRTP, typed/scope, prim registration, composition arcs, host | none notable |
| scene | 29 / 4993 | 167 | animation sampler/apply, xform-op stack, node systems, light frame/properties, camera properties, layout, instance overrides, mesh prims, gprim, skins, expressions | `update_node_transforms` (dirty ordering, owner writes, Scope pass-through), `Trs_transform` decompose, `sever_host` teardown |
| property | 20 / 4233 | 148 | registry, every value type, computed, defaults, elision, observers, inheritance, style, reference, animated layer, sealing, bridge, expressions, enum, strings, sets, attached groups, object references | threading claims (untestable as stated) |
| primitive | 8 / 1699 | 46 | mesh optimizer, attribute encodings, material textures/style/authored values, optimized variant | `Primitive_builder` index generation, `Buffer_writer`, `Primitive_shape` locking, `Triangle_soup` |
| geometry | 19 / 5128 | 148 | selective normals, chamfer, plane intersection, clip tile tree, CC creases, component remap, capsule, lattice, sharpness, seams, serialization, subdivision chain, convex hull, CSG | shapes beyond capsule, Conway operators other than chamfer, `Geometry::process` flags |
| gltf | 0 (data/variants.gltf only) | 0 | nothing at library level; round trip verified by scripts/scene_roundtrip_verify.py through the editor (doc/editor/scene_serialization.md); the memory bank records an undiagnosed async failure on that data file (memory-bank/topics/scenes_and_assets.md) | the whole importer/exporter |
| usd | 30 / 13761 | 405 | references, variants, sublayers, skinning, time samples, physics, node graphs, point instancers, materials, textures, draw modes, folders, styles, brushes, class prototypes | build-gated to Windows/Android (doc/testing.md:14) |
| physics | 15 / 2779 | 117 | joint reach, six-DOF classifier, drives, limits, overlap, world events, filters, shape descriptors, hull builder, activation, enforced limits per backend | Jolt-specific shapes, `save_state` / `restore_state` |
| math | 3 / 947 | 64 | projection, Euler angles | `Aabb`, `Sphere`, `Viewport`, `Input_axis`, `math_util` |
| commands | 3 / 513 | 15 | binding overrides, binding desc parse | dispatch, priority, drag/mouse state machine, XR |
| graph | 5 / 763 | 19 | sort, edit, connect, node copy | none notable |
| message_bus | 0 | 0 | nothing | policy dispatch, pruning, double buffering |

Cases are `TEST(`/`TEST_F(`/`TEST_P(` occurrences (grep); the gap that
matters is `gltf`, the native file format, untested at 9.3k lines.

--------------------------------------------------------------------------------
## 6. Future options (prioritized)

1. glTF unit tests (low cost, high value). `parse_gltf` is device-free
   (gltf_fastgltf.hpp:364-388) and `export_gltf` returns a string (554-561):
   build -> export -> parse -> compare needs no editor. Start with the
   `ERHE_node.properties` path and data/variants.gltf, which already
   reproduces a bug.

2. Move editor bits out of `Item_flags` / `Item_type` (medium cost, high
   value for reuse and compile scope). Helps: `Item_filter` and `is<T>()`
   need only bit positions. Hinders: both words are near full; USD `purpose`
   derives from four editor bits (item.hpp:213-219). A reserved bit range
   with app-registered label tables is the incremental step;
   `hosted_selection` moves to the editor's `Selection` in the same change.

3. Property system completion (medium). Mirror or cache `Light` reads the
   way `Camera` and `Scene` do (1.3); make `Mesh_primitive` members private;
   the items of doc/erhe/property_system.md section 6 and
   doc/plans/gltf_properties_extension.md; and one notification design,
   routing `Scene_host` callbacks and `INode_system` through property
   observers or the reverse.

4. Data-model-level undo (medium cost, high value). `read_local_state` /
   `apply_local_state` capture the exact local layer per property
   (dependency_object.hpp:293-300); a generic "record every
   `Property_changed_args` delivered while an operation runs" would replace
   the property halves of the editor's 34 operation classes (editor report
   1.6). Structural changes stay bespoke, modelled on the inheritance
   snapshot (hierarchy.cpp:204-244).

5. Scripting bindings (low-medium). The registry is reflective
   (dependency_property.hpp:129-174, property_string.cpp) and MCP is already
   a binding over it; a Lua or Python layer binds untyped `Dependency_object`
   access, `find_by_path` (hierarchy.hpp:206) and the operation stack with
   no per-class code.

6. Header diet (low). Split the flag/type tables out of item.hpp;
   forward-declare the registry; move the geogram include out of
   primitive/build_info.hpp and the profiler include out of item_host.hpp.

7. Keep glTF native, do not make USD native (decision, no cost). USD is
   20.5k lines over a fork building on two platforms (scene_format_support.md:102);
   the prim-level model already round-trips what erhe holds.

8. Data-oriented storage behind the object model (high cost, medium value
   today). Transform storage is isolated in `Node_data` (node.hpp:53-65) and
   every write funnels through one call, so a structure-of-arrays pool would
   keep the API. Not worth it before frustum culling (rendering report 6.1).

9. Instancing and LOD (medium-high). Instancers expand into child prims
   (point_instancer.hpp:13-22) while `Primitive` is already shared
   (mesh.hpp:74); an instance prim needs a draw-list instance record and an
   ID-pass mapping. `Purpose::proxy` is reserved (item.hpp:25-30) and
   `render_proxy` / `proxy_hidden` exist (100-111), so LOD can ride them.

10. Multi-scene, streaming, asset database (medium). One `Scene` per
    `Scene_host` with its own mutex and prim index exists
    (item_host.hpp:43-46), glTF 2.1 `externalAssets` are parsed
    (gltf_fastgltf.hpp:64-90) and uids give stable identity
    (item.hpp:786-793). Missing: a load policy for deferred subtrees and a
    library-level uid -> file/type/dependency index.

11. Small layering fixes (low): route `IWorld::debug_draw` through
    `IDebug_draw`; guard the XR includes in commands.hpp; drop the
    `erhe_item` -> `erhe::message_bus` link; make `erhe::item` PUBLIC in
    graph's CMake; delete `imotion_state.hpp`, `Node_data::diff_mask`,
    node.cpp:469-500 and the 41 in-tree review/readme files; turn the 17
    fatal copy constructors into `= delete`.
